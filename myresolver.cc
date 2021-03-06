#include "myresolver.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "base64.h"

/***usings***/
using std::memcpy;
using std::cout;
using std::endl;
/*************************************************************************
* MY RESOLVER CLASS 
**************************************************************************/
  void MyResolver::myResolver(){
    getDNSAddresses();
    packetCount = 0;
  }


  //Adds different DNS addresses. Backups in case one or more fails
  void MyResolver::getDNSAddresses(){
    DNSAddresses.push_back(createIPVector(198,41,0,4));
    DNSAddresses.push_back(createIPVector(192,228,79,201));
    DNSAddresses.push_back(createIPVector(192,33,4, 12));
    DNSAddresses.push_back(createIPVector(199,7,91,13));
    DNSAddresses.push_back(createIPVector(192, 203, 230,10));

    //add the strings to the string array of the root servers 
    DNSRootAddr.push_back(ROOT_SERVER_0);
    DNSRootAddr.push_back(ROOT_SERVER_1);
    DNSRootAddr.push_back(ROOT_SERVER_2);
    DNSRootAddr.push_back(ROOT_SERVER_3);
    DNSRootAddr.push_back(ROOT_SERVER_4);
  }
  
  //method that sends the packet to the correct server 
  void MyResolver::sendPacket(const char * destAddress){
    
    unsigned char buffer[65536], *qname;
  
    int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    
    if (handle <= 0){
      cout << "Socket creating failed" << endl;
      exit(1);
    }
    
    unsigned int DNSAddress = (DNSAddresses[0][0] << 24) | (DNSAddresses[0][1] << 16) | (DNSAddresses[0][2] << 8) | (DNSAddresses[0][3]);
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(destAddress);
    address.sin_port = htons(53);

    //set up the DNS Structure to standard queries
    DNSHeader *dns = (DNSHeader *)&buffer;

    //set up header packet 
    dns->ID = (unsigned short)htons(getpid()); 
    dns->QR = 0; // This is a query
    dns->Opcode = 0; //standard query
    dns->AA = 0; //not athoritative
    dns->TC = 0; // not truncated
    dns-> RD = 0; //Recursion set
    dns->RA = 0; //TODO: not really sure what this is??? recursion not avaliable??
    dns->Z = 0;
    dns->AD = 0;
    dns->CD = 0;
    dns->RCODE = 0;
    dns->QDCOUNT = htons(1);
    dns->ANCOUNT = 0;
    dns->NSCOUNT = 0;
    dns->ARCOUNT = htons(1);

    //The question section 
    qname = (unsigned char*)&buffer[sizeof(DNSHeader)];
    ChangetoDnsNameFormat(qname,(unsigned char*)URL.c_str()); //make sure this is working
    DNSQuestion* qinfo = (DNSQuestion*)&buffer[sizeof(DNSHeader)+(strlen((const char*)qname)+1)];
    
    if(strcmp(ipType.c_str(),"AAAA") == 0)
      qinfo->QTYPE = htons(28); // ipv4 address request
    else if(strcmp(ipType.c_str(),"A") == 0)
      qinfo->QTYPE = htons(1); // ipv6 address request


    DNS_RRSIG_Request *AddRecordsReq = (DNS_RRSIG_Request*)&buffer[sizeof(DNSHeader)+(strlen((const char*)qname)+1)+sizeof(DNSQuestion)-1]; 
    AddRecordsReq->Name = 0;
    AddRecordsReq->Type = htons(41);
    AddRecordsReq->payloadSize=htons(4096);
    AddRecordsReq->higher_bits=0;
    AddRecordsReq->EDNS0_v=0;
    AddRecordsReq->z=htons(32768); // 0x8000 request rrsig records
    AddRecordsReq->length=0;


    qinfo->QCLASS = htons(1); //internet

    if (sendto(handle,(char*)buffer,sizeof(DNSHeader)+ (strlen((const char*)qname)+1) + sizeof(DNSQuestion)+sizeof(DNS_RRSIG_Request)-1,0,(struct sockaddr*)&address,sizeof(address))==-1)
    {
      printf("%d Bytes failed to send. \n",handle);
      exit(0);
    }
    
    //recieving -----------
    int i = sizeof(address);    
    if (recvfrom(handle, (char*)buffer, 65536, 0, (sockaddr *)&address, (socklen_t *)&i) < 0)
    {
      cout << "Recv Failure" << endl;
    }
    
    
    readPacket(buffer, dns, qname);

  }
  
  //Read Packet is used to read the incoming pacekt from the DNS server
  void MyResolver::readPacket(unsigned char * buffer, DNSHeader *dns, unsigned char* qname){
    
    
    DNSHeader *dnsHeadRecv = (DNSHeader*)&buffer;
    unsigned char *dnsQueryRecv = (unsigned char*)&buffer[sizeof(DNSHeader)];
    

    //check for NXDOMAIN error ----
    int Nxflag = *(buffer+3)&0xF;
    if(Nxflag == 3)
    {
        printf("%s IS A NXDOMAIN\n", URL.c_str());
        exit(-1);
    }

    // make sure we recieved answers 
    if(ntohs(dns->ANCOUNT) > 0 ){
    
      unsigned char *dnsAnswerSection;
      dnsAnswerSection = &buffer[sizeof(DNSHeader) + (strlen((const char *)qname) +1) + sizeof(DNSQuestion)];
      DNSQuestion *dnsQuestionSection = (DNSQuestion*)&buffer[sizeof(DNSHeader)+(strlen((const char *)qname)+1)];
      

      //NOW WE ARE AT THE R_DATA PART
      DNS_ResRec DNSAnswers[(dns->ANCOUNT)];
      int stop = 0;
    
      for (int i = 0; i < ntohs(dns->ANCOUNT); ++i)
      {


        // printf("===========ANSWER %d: ===========\n",i);
        DNSAnswers[i].name=ReadName(dnsAnswerSection, buffer, &stop);
        dnsAnswerSection+=stop;
        DNSAnswers[i].resource = (R_DATA*)(dnsAnswerSection);
        dnsAnswerSection+=sizeof(R_DATA)-2;

       
        if(ntohs(DNSAnswers[i].resource->RDLENGTH)!=4 && ntohs(DNSAnswers[i].resource->RDLENGTH)!=16 && ntohs(DNSAnswers[i].resource->TYPE)!=46) // not an ipadress its a cname????
        {
            DNSAnswers[i].rdata = ReadName(dnsAnswerSection,buffer,&stop);
            dnsAnswerSection+=stop;
              printf("%s\t", DNSAnswers[i].name);
            printf("%d\t", ntohl(DNSAnswers[i].resource->TTL));
            printf("IN\t");
            switch(ntohs(DNSAnswers[i].resource->TYPE))
            {
              case 1:
                  printf("A\t");
              break;
              case 28:
                  printf("AAAA\t");
                  
              break;
              case 5:
                  printf("CNAME\t%s\n",DNSAnswers[i].rdata);
                  URL = (char *)DNSAnswers[i].rdata;
                  sendPacket(DNSRootAddr[0].c_str());
                  case 46:
                  printf("RRSIG\t");
              break;
            }
        }
        else if (ntohs(DNSAnswers[i].resource->RDLENGTH)==4) // ipadress found
        {
          printf("%s\t", DNSAnswers[i].name);
          printf("%d\t", ntohl(DNSAnswers[i].resource->TTL));
          printf("IN\t");
          printf("A\t");

          DNSAnswers[i].rdata = (unsigned char *) ReadIPv4Address(dnsAnswerSection,buffer,&stop);
          dnsAnswerSection+=4;
          printf("%s",DNSAnswers[i].rdata);

    	    if(i == ntohs(dns->ANCOUNT)-1)
          {
    	      printf("\n");
    	      exit(0);
    	    }
        }
        else if (ntohs(DNSAnswers[i].resource->RDLENGTH)==16) // ipadress found
        {
          printf("%s\t", DNSAnswers[i].name);
          printf("%d\t", ntohl(DNSAnswers[i].resource->TTL));
          printf("IN\t");
          printf("AAAA\t");

          DNSAnswers[i].rdata = (unsigned char *) ReadIPv6Address(dnsAnswerSection,buffer,&stop);
          dnsAnswerSection+=16;
          printf("%s",DNSAnswers[i].rdata);

    	    if(i == ntohs(dns->ANCOUNT)-1)
          {
    	      printf("\n");
    	      exit(0);
    	    }
        }
        else if(ntohs(DNSAnswers[i].resource->TYPE)==46) // this bracket is for RRSIG
        {
            int nameSize = 0;
            string ARecType = (ntohs(*((unsigned short *)dnsAnswerSection)))? "AAAA":"A";
            printf("%s\t", DNSAnswers[i].name);
            printf("%d\t", ntohl(DNSAnswers[i].resource->TTL));
            printf("IN\t");
            printf("RRSIG\t");
            printf("%s\t",ARecType.c_str());
            dnsAnswerSection+=2;// add two bytes to skip the type covered

            DNS_RRSIG* rrsigRec =(DNS_RRSIG*)&(dnsAnswerSection); 
            printf("%01X\t",*dnsAnswerSection);
            dnsAnswerSection++;        
            printf("%01x\t",*dnsAnswerSection); //label 
            dnsAnswerSection++;
            printf("%d\t",ntohl(*((unsigned int*)dnsAnswerSection))); // original TTL
            dnsAnswerSection+=4;
            printf("%d\t",ntohl(*((unsigned int *)dnsAnswerSection)) ); //sigExp
            dnsAnswerSection+=4;
            printf("%d\t",ntohl(*((unsigned int *)dnsAnswerSection)) ); //Sig Inc
            dnsAnswerSection+=4;
            printf("%d\t", ntohs(*((unsigned short *)dnsAnswerSection)) ); //
            dnsAnswerSection+=2;

        

            // now read the names 
            dnsAnswerSection+=nameSize;

            unsigned int dl = ntohs(DNSAnswers[i].resource->RDLENGTH) - nameSize -18;

            string signature = base64_encode((unsigned char const*)dnsAnswerSection, dl);
            printf("%s\n",signature.c_str());
            dnsAnswerSection+=dl;

            if(i == ntohs(dns->ANCOUNT)-1){
              exit(0);
            }
        }
        
        printf("\n");  
      } //end of for loop
    }
    else if(ntohs(dns->NSCOUNT) > 0 )
    {
      packetCount += 1;
      unsigned char *dnsANSection;
      dnsANSection = &buffer[sizeof(DNSHeader) + (strlen((const char *)qname) +1) + sizeof(DNSQuestion)];
      DNSQuestion *dnsQuestionSection = (DNSQuestion*)&buffer[sizeof(DNSHeader)+(strlen((const char *)qname)+1)];
     
      DNS_ResRec DNSNameServers[(dns->NSCOUNT)];
      int stop = 0;
      
      for (int i = 0; i < ntohs(dns->NSCOUNT); ++i)
      {
	
        DNSNameServers[i].name=ReadName(dnsANSection, buffer, &stop);
        //printf("i: %d\n",i );
        dnsANSection+=stop;
        DNSNameServers[i].resource = (R_DATA*)(dnsANSection);
        // int prevStop = stop;
        // printf("INCREMENTING THIS THIS MUCH %d\n",sizeof(unsigned int) );
        dnsANSection+=sizeof(R_DATA)-2;
	
      	if(ntohs(DNSNameServers[i].resource->TYPE) ==  2)
        {
      	  DNSNameServers[i].rdata = ReadName(dnsANSection,buffer,&stop);
          dnsANSection+=stop;
      	}
      	else
        {
      	  dnsANSection+=ntohs(DNSNameServers[i].resource->RDLENGTH);
      	}
      }
      
      DNS_ResRec DNSAddRecords[(dns->ARCOUNT)];
      
      for(int i = 0; i < ntohs(dns->ARCOUNT)-1; i++)
      {
        //printf("===========Additional %d: ===========\n",i);

        DNSAddRecords[i].name=ReadName(dnsANSection, buffer, &stop);
        dnsANSection+=stop;
        DNSAddRecords[i].resource = (R_DATA*)(dnsANSection);
        dnsANSection+=sizeof(R_DATA)-2;

        if (ntohs(DNSAddRecords[i].resource->RDLENGTH)==4) // ipaddress found
        {           
          DNSAddRecords[i].rdata = (unsigned char *) ReadIPv4Address(dnsANSection,buffer,&stop);
          dnsANSection+=4;

        }
        if (ntohs(DNSAddRecords[i].resource->RDLENGTH)== 16) // ipv6 found. 0x10 == 16 bytes
        {
            DNSAddRecords[i].rdata = (unsigned char *) 
            ReadIPv6Address(dnsANSection,buffer,&stop);
            dnsANSection+=16;
        }
      }
      
      
      for(int i = 0; i < ntohs(dns->NSCOUNT)-2; i++){
        for(int j = 0; j < ntohs(dns->ARCOUNT)-1; j++){
          if(strcmp((const char *)DNSNameServers[i].rdata, (const char *)DNSAddRecords[j].name) == 0){
            sendPacket((const char *)DNSAddRecords[j].rdata);
            break;
          }
            
	      }
      }
    }
    
  }
  
  vector<unsigned int> MyResolver::createIPVector(unsigned int a, unsigned int b, unsigned int c, unsigned int d){
    vector<unsigned int> temp;
    temp.push_back(a);
    temp.push_back(b);
    temp.push_back(c);
    temp.push_back(d);
    return temp;
  }
  
char* ReadIPv4Address(unsigned char* reader,unsigned char*buffer, int*count)
{
    char *IP_addr;
    IP_addr = (char *)malloc(16);
    char str[5];
    int counter = 0;
    int addrcounter = 0;

    for (int i = 0; i < 4; i++)
    { 
        int num = (unsigned int)*reader;
        sprintf(str, "%d", num);
        reader++;

          for(int j = 0; j< strlen(str);j++ )
          {
            IP_addr[addrcounter] = str[j];
            counter++;
            addrcounter++;
          }
        if(i<3)
        {
            IP_addr[addrcounter] = '.';
            addrcounter++;
        }
    }
    return IP_addr;

}
char* ReadIPv6Address(unsigned char* reader,unsigned char*buffer, int*count)
{
    char *IP_addr;
    int addrcounter = 0;
    int counter = 0;
    IP_addr = (char *)malloc(40);

    for (int i = 0; i < 16; i++)
    {
        char str[9];
        int num = (unsigned int)*reader;
        sprintf(str, "%02X", num);
        reader++;

  	    for(int j = 0; j< strlen(str);j++ )
        {
          IP_addr[addrcounter] = str[j];
          counter++;
          addrcounter++;
        }
          
        if(i < 15 && i%2==1)
        {
          IP_addr[addrcounter] = ':';
          addrcounter++;
        }
    }

    return IP_addr;

}
/****************************************************************
*this converts 3www6google3com to www.google.com
* DNSFormat to string readable format 
****************************************************************/
unsigned char* ReadName(unsigned char* reader,unsigned char* buffer,int* count)
{
    unsigned char *name;
    unsigned int p=0,flag=0,offset;

    *count = 1;
    name = (unsigned char*)malloc(256);
 
 
    //Read the DNS formatted string 
    while(*reader!=0)
    {
        if(*reader>=0xC0) // compression findname code -----
        {
            offset = *(reader+1);
            reader = buffer + offset - 1;
            flag = 1;
        }
        else // read the string like normal
        {
            name[p]=*reader;
            p++;
        }

        reader = reader+1; // move to the next 
 
        if(flag==0)
        {
            *count = *count + 1; //if we haven't jumped to another location then we can count up
        }
    }
    name[p]='\0'; //string complete (null terminator)

    if(flag==1) // add one because we jumped to a new location 
    {
        *count = *count + 1; 
    }
 
    //change from DNSformat to human readable format
    //example: 3www6google3com0 to www.google.com
    for(int i=0;i<(int)strlen((const char*)name);i++) 
    {
        p=name[i];
        for(int j=0;j<(int)p;j++) 
        {
            name[i]=name[i+1];
            i++;
        }
        name[i]='.';
    } 

    return name;
}

void ChangetoDnsNameFormat(unsigned char* dns,unsigned char* host)
{
    int lock=0 , i;
 
    strcat((char*)host,".");
 
    for(i=0 ; i<(int)strlen((char*)host) ; i++)
    {
        if(host[i]=='.')
        {
            *dns++=i-lock;
            for(;lock<i;lock++)
            {
                *dns++=host[lock];
            }
            lock++; //or lock=i+1;
        }
    }
    *dns++='\0';

}

  
bool checkIPType(char* ipType){
    if(strcmp(ipType, "A") == 0 || strcmp(ipType, "AAAA") == 0){
      return true;
    }
    return false;
}

int URLUsage(char* arg0){
  cout << "Usage: " << arg0 << " URL [A or AAAA]" << endl;
  cout << "where URL the address you want to find" << endl;
  cout << "[A or AAAA] is whether you want ipv4 or ipv6 address. Defaults to A" << endl;
  return -1;
} 
/**************************************************************************************
* This will print out the headerPacket recived from the buffer
*
***************************************************************************************/
void printDNSHeader(DNSHeader *header)
{
    printf("THE DNS HEADER: \n");
    printf("ID: %d\n",header->ID &0x01);
    printf("RD: %d\n",header->RD &0x01);
    printf("TC: %d\n",header->TC&0x01);
    printf("AA: %d \n",header->AA&0x01);
    printf("Opcode: %d \n",header->Opcode & 0x0F);
    printf("QR: %d\n", header->QR &0x01);
    printf("RCODE: %d\n",header->RCODE & 0x0F );
    printf("CD: %d\n", header->CD &0x01);
    printf("AD: %d\n", header->AD&0x01);
    printf("Z: %d\n", header->Z&0x01);
    printf("RA: %d\n", header->RA&0x01); 
    printf("QDCOUNT: %d \n", ntohs(header->QDCOUNT));
    printf("ANCOUNT: %d \n", ntohs(header->ANCOUNT));
    printf("NSCOUNT: %d \n", ntohs(header->NSCOUNT));
    printf("ARCOUNT: %d \n", ntohs(header->ARCOUNT));
}



/********MAIN METHOD ********/

int main(int argc, char* argv[]){
  if (!(argc == 2 || argc == 3)) return URLUsage(argv[0]);

  
  MyResolver mR;
  mR.myResolver();  
    
  if(argc == 2){
    mR.ipType = "A";
  }
  
  if(argc == 3){
    if (checkIPType(argv[2])){
      mR.ipType = argv[2];
    }
    else{
      return URLUsage(argv[0]);
    }
  }
  
  mR.URL = argv[1];
  
  //208.67.222.222
  //192.58.128.30
  mR.sendPacket(mR.DNSRootAddr[0].c_str());
}
