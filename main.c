#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
//#include <pthread.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_INTERFACES		10
#define MY_MULTICAST_ADDR	"234.5.6.78"
#define MY_MULTICAST_PORT	"7788"
#define MY_SERVER_PORT		"7575"
#define MY_INTERFACE_NAME	"wlan0"
#define COMPANY_ID			"Modiotek"
#define ACK_STRING			"multicast_ack"
#define STATE_COMPLETE		0
#define STATE_WAIT_ACK		1
#define STATE_WAIT_CRFM		2
#define MULTICAST_SEND_CNT	60
#define WAIT_ACK_TIMEOUT	MULTICAST_SEND_CNT
#define WAIT_CRFM_TIMEOUT	180
#define PRE_DOORBELL_ID		"Doorbell-"
#define RANDOM_DIGITAL		9

typedef int				BOOL;
typedef unsigned char	BYTE;
#define FALSE 	0
#define TRUE 	1

//void ListAllInterface(int);
BOOL FindInterface(int, char*);
char *g_pszDoorbellID;
BOOL g_bStopMulticast;
char g_szFinalDoorbellID[256]={0};
char g_szPhotoServerAddress[256]={0};

void GenerateRandomNameString(char *szName, int digital)
{
	int nValue, nRandom;
	int i, len, total, cnt, n;
	char *form;
	
	len =strlen(PRE_DOORBELL_ID);
	cnt=0;
	n=digital;
	do
	{
		cnt++;
		n /= 10;
	}	while(n);
	total = len + 3 + cnt + 1;
	form=(char*)malloc(sizeof(char)*total);
	
	sprintf(form, "%s%%0%dd", PRE_DOORBELL_ID, digital);
	for(i=0, nValue=1; i< digital; i++)
		nValue *= 10;
	
	srand(time(NULL));
	nRandom = rand();
	nRandom %= nValue;
	sprintf(szName, form, nRandom);
	printf("generate random name : \"%s\"\n", szName);
	free(form);
}

void *ServerThread(void *data)
{
	int							nCurrState;
	int 						nServerFD;
	struct sockaddr_in			ServerAddr;
	struct sockaddr_in			ClientAddr;
	int							nPort;
	int							nClientFD;
	BYTE						byBuffer[1024];
	char						szBuffer[1024]={0};
	int							nReadBytes;
	socklen_t					nAddrLen;
	fd_set 						RDFDSET;
	struct timeval				RDTimeOut;
	int 						rc;	
	char						*token=NULL;
	int							nAckState;
	int							nTimeout;
	
	
	nServerFD = socket(PF_INET, SOCK_STREAM, 0);
	if(nServerFD == -1)
	{
		fprintf(stderr, "Open Server socket failed due to %d : %s\n", errno, strerror(errno));
		goto OOS;
	}
	memset(&ServerAddr, 0, sizeof(struct sockaddr_in));
	nPort = atoi(MY_SERVER_PORT);
	
	ServerAddr.sin_family 		= AF_INET;
	ServerAddr.sin_addr.s_addr	= INADDR_ANY;
	ServerAddr.sin_port			= htons(nPort);
	
	bind(nServerFD, (struct sockaddr*)&ServerAddr, sizeof(ServerAddr));
	nAddrLen = sizeof(ClientAddr);
	nCurrState = STATE_WAIT_ACK;
	listen(nServerFD, 1);
	while(nCurrState != STATE_COMPLETE)
	{
		nClientFD = accept(nServerFD, (struct sockaddr*)&ClientAddr, &nAddrLen);
		if(nCurrState == STATE_WAIT_ACK)
		{
			//=======================================================================
			//	Get multicast ACK from someone
			//-----------------------------------------------------------------------
			FD_ZERO(&RDFDSET);
			FD_SET(nClientFD, &RDFDSET);
			RDTimeOut.tv_sec=WAIT_ACK_TIMEOUT;
			RDTimeOut.tv_usec=0;
			rc = select(nClientFD + 1, &RDFDSET, NULL, NULL, &RDTimeOut);
			if(rc == 0)
			{
				fprintf(stderr, "Timeout to get ACK string\n");
				g_bStopMulticast = TRUE;
				close(nClientFD);
				goto CLOSE_SERVER;
			}
			else if (rc < 0)
			{
				fprintf(stderr, "Get socket error %d : %s\n", errno ,strerror(errno));
				g_bStopMulticast = TRUE;
				close(nClientFD);
				goto CLOSE_SERVER;
			}
			else
			{
				//nReadBytes = recv(nClientFD, byBuffer, sizeof(byBuffer), 0);
				nReadBytes = read(nClientFD, byBuffer, sizeof(byBuffer));
				if(nReadBytes == 0)
				{
					fprintf(stderr, "The connection is broken...\n");
					g_bStopMulticast = TRUE;
					close(nClientFD);
					goto CLOSE_SERVER;
				}
				memcpy(szBuffer, byBuffer, nReadBytes);
				szBuffer[nReadBytes]='\0';
				printf("Get ACK string %s\n", szBuffer);
				//	Get token : ack string and timeout time
				nAckState=0;
				token=strtok(szBuffer, ":");
				while (token != NULL)
				{
					if(nAckState == 0)
					{
						if(strcmp(token, ACK_STRING))
						{
							fprintf(stderr, "Unkonwn ACK string :\"%s\"\n", token);
							g_bStopMulticast = TRUE;
							close(nClientFD);
							goto CLOSE_SERVER;
						}
						nAckState++;
					}
					else if(nAckState == 1)
					{
						nTimeout = atoi(token);
						if(nTimeout <= 0)
							nTimeout = WAIT_CRFM_TIMEOUT;
						printf("Timeout = %d\n", nTimeout);
					}	
					token=strtok(NULL, ":");
				}
				g_bStopMulticast = TRUE;
				nCurrState = STATE_WAIT_CRFM;
				close(nClientFD);
				continue;
			}
			//=======================================================================
		}
		else if(nCurrState == STATE_WAIT_CRFM)
		{	
			//=======================================================================
			//	Get confirmed ID from someone
			//-----------------------------------------------------------------------
			FD_ZERO(&RDFDSET);
			FD_SET(nClientFD, &RDFDSET);
			RDTimeOut.tv_sec=nTimeout;
			RDTimeOut.tv_usec=0;
			rc = select(nClientFD + 1, &RDFDSET, NULL, NULL, &RDTimeOut);
			if(rc == 0)
			{
				fprintf(stderr, "Timeout to get confirmed id string\n");
				close(nClientFD);
				goto CLOSE_SERVER;
			}
			else if (rc < 0)
			{
				fprintf(stderr, "Get socket error %d : %s\n", errno ,strerror(errno));
				close(nClientFD);
				goto CLOSE_SERVER;
			}
			else
			{
				//nReadBytes = recv(nClientFD, byBuffer, sizeof(byBuffer), 0);
				nReadBytes = read(nClientFD, byBuffer, sizeof(byBuffer));
				if(nReadBytes == 0)
				{
					fprintf(stderr, "The connection is broken...\n");
					close(nClientFD);
					goto CLOSE_SERVER;
				}
				memcpy(szBuffer, byBuffer, nReadBytes);
				szBuffer[nReadBytes]='\0';
				printf("Get confirmed string \"%s\"\n", szBuffer);
				nAckState=0;
				token=strtok(szBuffer, ",");
				while (token != NULL)
				{
					if(nAckState == 0)
					{
						strcpy(g_szPhotoServerAddress, token);
						printf("Photo Server Address = %s\n", g_szPhotoServerAddress);
						nAckState++;
					}
					else if(nAckState == 1)
					{
						strcpy(g_szFinalDoorbellID, token);
						printf("Doorbell ID = %s\n", g_szFinalDoorbellID);
						g_pszDoorbellID = g_szFinalDoorbellID;
					}	
					token=strtok(NULL, ",");
				}
				nCurrState = STATE_COMPLETE;
				close(nClientFD);
				continue;
			}
			//=======================================================================
		}
	}

CLOSE_SERVER:
	close(nServerFD);
OOS:	
	return NULL;
}


int main (int argc, char *argv[])
{
	int 				sfd, rc, nExitCode=0, i;
	pthread_t 			ServerThreadID;
	pthread_attr_t		ServerThreadAttr;
	void				*pStatus;
	struct ifreq		ifr;
	struct in_addr 		MeinAdress, multicastaddr;
	struct sockaddr_in	GroupSockAddr;
	unsigned char		uValue;
	char				szMyAddr[16]={0};
	char				szMessage[1024]={0};
	int					nPort=0;	
	char				szRandomName[256]={0};
	
	
	GenerateRandomNameString(szRandomName, RANDOM_DIGITAL);
	g_pszDoorbellID = szRandomName;
	
	sfd = socket(PF_INET, SOCK_DGRAM, 0);
	if(sfd == -1)
	{
		fprintf(stderr, "Open socket fail due to %d : %s\n", errno, strerror(errno));
		nExitCode= -1;
		goto solong;
	}
	if(!FindInterface(sfd, MY_INTERFACE_NAME))
	{
		fprintf(stderr, "The interface %s does not exist\n", MY_INTERFACE_NAME);
		nExitCode= -1;
		goto byebye;
	}
	memset(ifr.ifr_name, 0, sizeof(ifr.ifr_name));
	strcpy(ifr.ifr_name, MY_INTERFACE_NAME);
	if(ioctl(sfd, SIOCGIFADDR, &ifr) == -1)
	{
		fprintf(stderr, "ioctl(SIOCGIFADDR) error %d : %s\n", errno, strerror(errno));
		nExitCode= -1;
		goto byebye;
	}
	MeinAdress.s_addr = ((struct sockaddr_in *)(&ifr.ifr_addr))->sin_addr.s_addr;
	printf("Get interface %s IP address : %s\n", MY_INTERFACE_NAME, inet_ntoa(MeinAdress));
	
	// Disable multicast loop
	uValue = 0;
	setsockopt(sfd, IPPROTO_IP, IP_MULTICAST_LOOP, &uValue, sizeof(uValue));
	// Set multicast interface address
	setsockopt(sfd, IPPROTO_IP, IP_MULTICAST_IF, &MeinAdress, sizeof(MeinAdress));
	
	strcpy(szMyAddr, inet_ntoa(MeinAdress));
	memset(szMessage, 0, sizeof(szMessage));
	sprintf(szMessage, "{\"company_id\":\"%s\",\"doorbell_id\":\"%s\",\"doorbell_addr\":\"%s\",\"doorbell_port\":\"%s\"}\n", 
		COMPANY_ID, g_pszDoorbellID, szMyAddr, MY_SERVER_PORT);
	printf("The send out message is %s", szMessage);
	nPort = atoi(MY_MULTICAST_PORT);
	
	inet_aton(MY_MULTICAST_ADDR, &multicastaddr);
	memset((void*)&GroupSockAddr, 0, sizeof(GroupSockAddr));
	GroupSockAddr.sin_family=AF_INET;
	GroupSockAddr.sin_addr.s_addr = multicastaddr.s_addr;
	GroupSockAddr.sin_port = htons(nPort);
	
	//	Prepare and start thread to lisent 
	pthread_attr_init(&ServerThreadAttr);
	rc = pthread_create(&ServerThreadID, &ServerThreadAttr, ServerThread, NULL);
	if(rc!=0)
	{
		fprintf(stderr, "pthread_create() return %d : %s\n", rc, strerror(rc));
		nExitCode= -1;
		goto byebye;
	}
	for(i=0, g_bStopMulticast=FALSE ; i< MULTICAST_SEND_CNT && !g_bStopMulticast; i++)
	{
		//	Send out the message
		if(sendto(sfd, szMessage, strlen(szMessage), 0, (struct sockaddr*)&GroupSockAddr, sizeof(GroupSockAddr)) < 0)
		{
			fprintf(stderr, "sendto() failed due to %d: %s\n", errno, strerror(errno));
			nExitCode= -1;
		}
		printf("Use multicast (%s:%s) to send out the message: %s\n", MY_MULTICAST_ADDR, MY_MULTICAST_PORT, szMessage);
		sleep(1);
	}
	pthread_join(ServerThreadID, &pStatus);
byebye:	
	close(sfd);
solong:	
	return nExitCode;
}

BOOL FindInterface(int sfd, char *szIf)
{
	struct ifreq		ifr[MAX_INTERFACES];
	struct ifconf		ifc;
	int					i;
	
	memset(ifr, 0, sizeof(ifr));
	ifc.ifc_len	= sizeof(ifr);
	ifc.ifc_req	= ifr;
	
	if(ioctl(sfd, SIOCGIFCONF, &ifc) == -1)
	{
		fprintf(stderr, "ioctl(SIOCGIFCONF) error %d : %s\n", errno, strerror(errno));
		return FALSE;
	}
	
	for(i=0; i< ifc.ifc_len / sizeof(struct ifreq); i++)
	{
		if(ifr[i].ifr_addr.sa_family != AF_INET)
			continue;
		if(strcmp(szIf, ifr[i].ifr_name) == 0 )
			return TRUE;
	}
	return FALSE;
}


/*
void ListAllInterface(int sfd)
{
	struct ifreq			ifr[MAX_INTERFACES];
	struct ifconf		ifc;
	int					i;
	struct sockaddr_in	*paddr;
	
	memset(ifr, 0, sizeof(ifr));
	ifc.ifc_len	= sizeof(ifr);
	ifc.ifc_req	= ifr;
	
	if(ioctl(sfd, SIOCGIFCONF, &ifc) == -1)
	{
		fprintf(stderr, "ioctl(SIOCGIFCONF) error %d : %s\n", errno, strerror(errno));
		return;
	}
	
	for(i=0; i< ifc.ifc_len / sizeof(struct ifreq); i++)
	{
		if(ifr[i].ifr_addr.sa_family != AF_INET)
			continue;
		printf(" %d). %s \n",i+1, ifr[i].ifr_name);
		if(ioctl(sfd, SIOCGIFADDR, &ifr[i]) == -1)
		{
			fprintf(stderr, "ioctl(SIOCGIFADDR) error %d : %s\n", errno, strerror(errno));
			return;
		}
		else
		{
			paddr=(struct sockaddr_in*)(&ifr[i].ifr_addr);
			printf("\t IP address %s\n", inet_ntoa(paddr->sin_addr));
		}
	}
	
}
*/



