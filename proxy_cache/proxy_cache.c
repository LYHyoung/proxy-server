////////////////////////////////////////////////////////////////////////////
// File Name		: proxy_cache.c                                   //
// Date			: 2021/05/28					  //
// Os			: Ubuntu 16.04 LTS 64bits			  //
// Author		: Lee Ye Hyeong					  //
// Student ID		: 2017202050					  //
// ---------------------------------------------------------------------- //
// Title : system Programming - proxy server				  //
// Description : When web browser's request are operated, only one client //
//		can send request by using semaphore. 			  //
//  		 And server gets HTTP request and make response to child  //
//		process, and check if it's MISS or HIT.			  //
//		 If it's HIT, server make response from cache file. And   //
//		if it's MISS, server make response and write to cache 	  //
//		file. Both HIT and MISS print to logfile.		  //
//		 Also when ctrl+c signal is occured, print terminate in	  //
//		logfile.						  //
//		 When the program tries to write in logfile, it should be //
//		approach by using thread.				  //
//		 But if there's No internet connection in 10 seconds, 	  //
//		this program should print "No Response" and terminate the //
//		child process.						  //
////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/sha.h>

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <ctype.h>
#include <signal.h>
#include <netdb.h>
#include <time.h>
#include <pthread.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <sys/ipc.h>
#include <sys/sem.h>

#define BUFFSIZE 	1024
#define PORTNO		39999

pid_t pid, web_pid;	// pid
char home[1000];	// home : directory position

FILE *fp_log;		// logfile
FILE *fp_c;		// cache file

int start_t;		// start time
int sub_p = 0;		// number of sub process

int semid;		// semaphore

union semun {				// make 'semun' union for semctl funcion
	int val;			// buffer for IPC
	struct semid_ds *buf;		
	unsigned short int *array;	
} arg;

time_t now;                     // get time data
struct tm *ltp;           	// local time

DIR *dirp;		// dirp : directory pointer, used for opendir, readdir
struct dirent *dp;	// dp : dirent structure pointer, used for opendir, readdir
	
struct sockaddr_in client_addr;	// socket address structure
int socket_fd, client_fd;


char Dir[4];		// directory name from hashed data
char FileName[42];	// URL's hashed data

pthread_t p_thread;	// thread's number
int thr_id;		// thread's ID (by using pthread_create)

////////////////////////////////////////////////////////////////////////////
// sigint_handler							  //
// ====================================================================== //
// (Input) signo :signal number						  //
//									  //
// (Purpose) When ctrl+c signal is occured.				  //
////////////////////////////////////////////////////////////////////////////
static void sigint_handler(int signo) {
	time_t end;
        struct tm *gtp;
        time(&end);           // get end time
        gtp = gmtime(&end);   // set in gmtime
        int end_t = mktime(gtp);           // t1 : starting time by gmtime
	
	if ((semctl(semid,0,IPC_RMID,arg)) == -1) {
		perror("semctl failed");
		exit(1);
	}
	
	// print running time and number of sub process
	fprintf(fp_log, "**SERVER** [Terminated] run time: %d sec. #sub process: %d \n", end_t-start_t, sub_p);
	pthread_exit(NULL);
	
	fclose (fp_log);	// close logfile
	exit(0);		// end program
}

////////////////////////////////////////////////////////////////////////////
// getIPAddr								  //
// ====================================================================== //
// (Input) addr : HOST data from buffer					  //
//									  //
// (Output) haddr : dotted IPv4 address					  //
//									  //
// (Purpose) Get IP Address from URL data(addr)				  //
////////////////////////////////////////////////////////////////////////////
char* getIPAddr(char *addr) {
	struct hostent* hent;		// host entry struct
	char* haddr;
	int len = strlen(addr);
	
	// check host entry
	if ( (hent = (struct hostent*)gethostbyname(addr)) != NULL) {
		haddr = inet_ntoa(*((struct in_addr*)hent->h_addr_list[0]));
	}
	
	return haddr;		// return header address
}

////////////////////////////////////////////////////////////////////////////
// sigalrm_handler							  //
// ====================================================================== //
// (Input) signo : signal number					  //
// (Purpose) When Alarm signal has occured kill child process		  //
////////////////////////////////////////////////////////////////////////////
static void sigalrm_handler(int signo) {	// when alarm signal occured
	printf("NO RESPONSE\n");		// print NO RESPONSE
	kill(pid,SIGCHLD);			// kill child process
	return;
}

////////////////////////////////////////////////////////////////////////////
// getHomeDir								  //
// ====================================================================== //
// (Input) home -> Used for Output					  //
//									  //
// (Output) char* - Home directory					  //
//									  //
// (Purpose) Get home directory						  //
////////////////////////////////////////////////////////////////////////////
char *getHomeDir(char *home) {
	struct passwd *usr_info = getpwuid(getuid());		// get user info
	strcpy(home, usr_info->pw_dir);		// get homedirectory in 'home'
	
	return home;		// return 'home'(home directory)
}

////////////////////////////////////////////////////////////////////////////
// sha1_hash								  //
// ====================================================================== //
// (Input) input_url -> Inputted URL					  //
// 	   hashed_url -> Used for Output				  //
//									  //
// (Output) char* - hashed URL						  //
//									  //
// (Purpose) Get hashed URL						  //
////////////////////////////////////////////////////////////////////////////
char *sha1_hash(char *input_url, char *hashed_url) {
	unsigned char hashed_160bits[20];
	char hashed_hex[41];
	int i;		// used in for loop
	
	SHA1(input_url, strlen(input_url),hashed_160bits);	// get hashed data of 'input_url' in 'hased_160bits'
	////////////////////// transfer to hexadecimal ///////////////////////
	for(i=0; i<sizeof(hashed_160bits); i++)
                sprintf(hashed_hex + i*2, "%02x", hashed_160bits[i]);
        //////////////////////// End of transfer /////////////////////////////
        strcpy(hashed_url, hashed_hex);		// copy hexadecimal data into 'hashed_url'
	
        return hashed_url;	// return hexadecimal data of URL
}

////////////////////////////////////////////////////////////////////////////
// t_MISS								  //
// ====================================================================== //
// (Input) url -> URL data						  //
//									  //
// (Purpose) Print MISS in logfile.					  //
////////////////////////////////////////////////////////////////////////////
void *t_MISS(void *url) {
	printf("*PID# %d create the *TID# %ld. \n", web_pid, p_thread);
	
	// MISS time
	time(&now);
	ltp = localtime(&now);
	
	// Print MISS URL and present time
	fprintf(fp_log, "[MISS]%s", (char *)url);
	fprintf(fp_log, "-[%d/%02d/%02d, %02d:%02d:%02d] \n", ltp->tm_year+1900, ltp->tm_mon+1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec);
	
	// disconnect		/* DO NOT PRINT in proxy3_2 Assignment */
	//printf("[%s : %d] client was disconnected\n", inet_ntoa(inet_client_address), client_addr.sin_port);
	
	printf("*TID# %ld is exited. \n", p_thread);
}

////////////////////////////////////////////////////////////////////////////
// t_HIT								  //
// ====================================================================== //
// (Input) url -> URL data						  //
//									  //
// (Purpose) Print HIT in logfile.					  //
////////////////////////////////////////////////////////////////////////////
void *t_HIT(void *url) {
	printf("*PID# %d create the *TID# %ld. \n", web_pid, p_thread);
	
	// HIT time
	time(&now);
	ltp = localtime(&now);  // local time
	
	// Print HIT URL's Directory and File's name
	fprintf(fp_log, "[HIT]%s/%s", Dir, FileName);
	fprintf(fp_log, "-[%d/%02d/%02d, %02d:%02d:%02d] \n", ltp->tm_year+1900, ltp->tm_mon+1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec);
	fprintf(fp_log, "[HIT]%s \n", (char *)url);
	
	// disconnect		/* DO NOT PRINT in proxy3_2 Assignment */
	//printf("[%s : %d] client was disconnected\n", inet_ntoa(inet_client_address), client_addr.sin_port);
	
	printf("*TID# %ld is exited. \n", p_thread);
}

////////////////////////////////////////////////////////////////////////////
// p									  //
// ====================================================================== //
// (Purpose) Insert client request to semaphore and check the URL if it's //
//	    MISS or HIT. 						  //
//	     If it's HIT, write response from cache file. If it's MISS,	  //
//	    make cache file and it's directory. Then write response for	  //
//	    client and also in cache file.				  //
//	     Both MISS and HIT should write it's information in logfile   //
//	    by using thread.						  //
////////////////////////////////////////////////////////////////////////////
void p(int semid) {

////////////////////////////////////// get URL data //////////////////////////////////////
        while(1) {
		struct in_addr inet_client_address;		// inner network address structure
		char result[41];	// result : URL's hashed data 
		int len;		// length of client address
		
		// socket error
		if ((client_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
			printf("Server : Can't open stream socket\n");
			return;
		}
		
		len = sizeof(client_addr);	// size of client address
		
		client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &len);	// accept client
		
		if (client_fd < 0)	// accept error
		{
			printf("Server : accept failed\n");
			return;
		}
		
		char buf[BUFFSIZE];	// buffer
		
		char response_header[BUFFSIZE] = {0, };		// response header
		char response_message[BUFFSIZE] = {0, };	// response message
		
		char tmp[BUFFSIZE] = {0, };		// temporary char array
		char url[BUFFSIZE] = {0, };		// URL data (by copy)
		
		char *tok = NULL;		// token for method
		
		
		inet_client_address.s_addr = client_addr.sin_addr.s_addr;	// set to client's address
		
		read(client_fd, buf, BUFFSIZE);		// read buffer data
		
		strcpy(tmp, buf);	// copy temporary variable from buffer
		
		tok = strtok(tmp, " ");
		if (strcmp(tok, "CONNECT") == 0) {	// if "CONNECT"
			tok = strtok(NULL, " ");	// no response
			continue;
		}
		else if (strcmp(tok, "POST") == 0) {	// if "POST"
			tok = strtok(NULL, " ");	// no response
			continue;
		}
		else if (strcmp(tok, "HEAD") == 0) {	// if "HEAD"
			tok = strtok(NULL, " ");	// no response
			continue;
		}
		
		else if (strcmp(tok, "GET") == 0) {		// if "GET"
			tok = strtok(NULL, " ");	// response and set URL
			strcpy(url, tok);
			
			char ico[4] = {0, };	// used for checking if URL is '(URL)/	favico.ico'
			for (int i=0;i<=3;i++) {	// getting URL's letter (ex. ico)
				ico[i] = url[strlen(url)-3+i];
			}
			// chrome set url 'favico.ico', so conitnue when ico is set to url
			if (strcmp(ico, "ico") == 0)
				continue;
		}
		
		char* check;			// check IP Address with 'check' HOST
		char t[BUFFSIZE];		// temporary variable for copy the 'buf'
		
		strcpy(t, buf);			// copy buf to t
		
		/* set 'check' to HOST(ex. info.kw.ac.kr) */
		check = strtok(t, " ");
		check = strtok(NULL, "/");
		check = strtok(NULL, "/");
		
		
		char* IPAddr;                   // IP Address
	        
	        IPAddr = getIPAddr(check);      // get IP Address
	        
		alarm(40);	// set the alarm
	        if (IPAddr == NULL) {           // internet connection is not well
	                while(1);		// stop work
			//printf("worked \n\n");
	        }
		alarm(0);	// off the alarm
		
		/* success print */	/* DO NOT PRINT in proxy3_2 Assignment */
	        // connect
		/*
		printf("[%s : %d] client was connected\n", inet_ntoa(inet_client_address), client_addr.sin_port);
		
		puts("=================================================");
		printf("Request from [%s : %d]\n", inet_ntoa(inet_client_address), client_addr.sin_port);
		puts(buf);
		puts("=================================================\n");
		*/
		
		sha1_hash(url,result);		// SHA1
		
		getHomeDir(home);		// get home directory
		
		/* get 'Dir' */
		for (int i=0; i<3; i++){
			Dir[i] = result[i];
		}
		Dir[3] = '\0';
		
		/* setting 'home' into ~/cache/'Dir' */
		strcat(home, "/cache/");
		strcat(home, Dir);
			
		/* FileName : file name from hash Data */
		for (int i=0; i<strlen(result)-3; i++){
			FileName[i] = result[i+3];
		}
		
		if (opendir(home) != NULL) {		// check if directory is already made
			dirp = opendir(home);	// dirp : open directory named 'Dir' (Dir : directory name from ashed data)
			dp = readdir(dirp);	// dp : get file from 'Dir'
			int check = 0;		// use for HIT flag 
				
			/* check all file */			
			while(dp->d_name != NULL){
				if (!strcmp(dp->d_name, FileName)){	// check if file name is same with 'FullName' (already made file)
					check = 1;  // HIT flag
					break;
				}
				dp = readdir(dirp);	// get next file
			}
			closedir(dirp);		// close 'dirp'
			
			/* HIT */
			if (check) {
				sprintf(response_message,
	                 		"<h1>HIT</h1><br>");		// set response message when "HIT"
				// set home as ~/cache/'Dir'/'FileName'
				strcat(home, "/"); strcat(home, FileName);
				
				FILE *fp_t2 = fopen(home, "r");	// open file in read mode
				char str_b[BUFFSIZE];		// str_b : check if HIT is already written
				
				while(feof(fp_t2) == 0){	// set str_b to last line of file
					fgets(str_b, BUFFSIZE, fp_t2);
				}
				fclose(fp_t2);			// close cache file
				
				if (strcmp(str_b, response_message) != 0) {	// check if HITd is already written in cache file
					fp_c = fopen(home, "a");	// write in cache file
					fprintf(fp_c, "%s", response_message);
					fclose(fp_c);
				}
				
	               		FILE *fp_t = fopen(home, "r");	// open as read option
				char str_a[BUFFSIZE];		// get HIT message to str_a
				while(feof(fp_t) == 0){
					fgets(str_a, BUFFSIZE, fp_t);
				}
				
				write(client_fd, str_a, strlen(str_a));	// write to web browser
				fclose(fp_t);
				
				// disconnect		/* DO NOT PRINT in proxy3_2 Assignment */
	               		//printf("[%s : %d] client was disconnected\n", inet_ntoa(inet_client_address), client_addr.sin_port);
				
				
				thr_id = pthread_create(&p_thread, NULL, t_HIT, (void *)&url);
				if (thr_id < 0) {
					perror("thread create error : ");
					exit(0);
				}
				
				
				struct sembuf pbuf;		// semaphore buffer struct
	     		   	pbuf.sem_num = 0;		// sem_num to zero
	        		pbuf.sem_op = -1;		// sem_op to one (re)
	        		pbuf.sem_flg = SEM_UNDO;	// SEM_UNDO (always)
	        		if ((semop(semid, &pbuf, 1)) == -1) {// operate the semaphore and if there's error
	                		perror("p : semop failed ");
	                		exit(1);
	        		}
				
		                close(client_fd);	// client close
				pthread_exit(NULL);
				break;
			}
		}
		
		/* MISS */
		// make directory named 'Dir'
		mkdir(home, S_IRWXU | S_IRWXG | S_IRWXO);
		strcat(home, "/"); strcat(home, FileName);
		
		/* use command "touch 'home'" for making file */
		char line[256] = { 0 };
		strcat(line, "touch ");
		strcat(line, home);
		system(line);		// same meaning as "touch 'home'"
		
		fp_c = fopen(home, "w");
		
		sprintf(response_message,
			"<h1>MISS</h1><br>");		// set response message about "MISS"
	
		sprintf(response_header,	// set response header about "MISS"
			"HTTP/1.0 200 OK\r\n"
			"Server: simple web server\r\n"
			"Content-length:%lu\r\n"
			"Content-type:text/html\r\n\r\n", strlen(response_message));
		
		// print buffer data and response message in cache file
		fprintf(fp_c, "%s \n", buf);
		fprintf(fp_c, "%s \n", response_message);
		fclose(fp_c);
		write(client_fd, buf, strlen(buf));	// write response header
		write(client_fd, response_message, strlen(response_message));	// write response message
		
		
		thr_id = pthread_create(&p_thread, NULL, t_MISS, (void *)&url);
		if (thr_id < 0) {
			perror("thread create error : ");
			exit(0);
		}
		
		// disconnect		/* DO NOT PRINT in proxy3_2 Assignment */
		//printf("[%s : %d] client was disconnected\n", inet_ntoa(inet_client_address), client_addr.sin_port);
		
		
		struct sembuf pbuf;				// semaphore buffer struct
	        pbuf.sem_num = 0;				// sem_num to zero
	        pbuf.sem_op = -1;				// sem_op to one (re)
	        pbuf.sem_flg = SEM_UNDO;			// SEM_UNDO (always)
	        if ((semop(semid, &pbuf, 1)) == -1) {		// operate the semaphore and if there's error
	                perror("p : semop failed ");
	                exit(1);
	        }
		close(client_fd);	// close client
		pthread_exit(NULL);
		break;
	}
////////////////////////////////////// end sub process //////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////
// v									  //
// ====================================================================== //
// (Purpose) delete what's inside the semaphore				  //
////////////////////////////////////////////////////////////////////////////
void v(int semid) {
        struct sembuf vbuf;				// semaphore buffer struct
        vbuf.sem_num = 0;				// sem_num to zero
        vbuf.sem_op = 1;				// sem_op to one (re)
        vbuf.sem_flg = SEM_UNDO;			// SEM_UNDO (always)
        if ((semop(semid, &vbuf, 1)) == -1) {		// operate the semaphore and if there's error
                perror("v : semop failed ");
                exit(1);
        }
}

////////////////////////////////////////////////////////////////////////////
// repeat								  //
// ====================================================================== //
// (Input) semid : semaphore number					  //
//	   web_pid : sub process's pid					  //
// (Purpose) critical section.						  //
////////////////////////////////////////////////////////////////////////////
void repeat(int semid)
{
	printf("*PID# %d is waiting for the semaphore.\n", web_pid);	// wait for sleep
	//sleep(3);	// sleep
	printf("*PID# %d is in the critiical zone.\n", web_pid);
        p(semid);						// insert to semaphore
        printf("*PID# %d exited the critical zone.\n", web_pid);
        v(semid);	// delete what's inside the semaphore
	
        exit(0);
}

////////////////////////////////////////////////////////////////////////////
// sys_fork								  //
// ====================================================================== //
// (Purpose) Perform making server and setting the semaphore.		  //
//	     After making server and semaphore by using while loop, sub	  //
//	    process will be made continuosly. In sub process the client   //
//          will be made in 'repeat' function.				  //
// 	     Also the sub process will be countted.			  //
////////////////////////////////////////////////////////////////////////////
void sys_fork() {
	signal(SIGINT, sigint_handler);	// when ctrl+c signal occured
	
	struct sockaddr_in server_addr;	// socket address structure
	
	
	// socket error
	if ((socket_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		printf("Server : Can't open stream socket\n");
		return;
	}
	
	/* set server_addr */
	bzero((char*)&server_addr, sizeof(server_addr));	// clear server_addr
	server_addr.sin_family = AF_INET;	// IPv4
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);	// change to network address
	server_addr.sin_port = htons(PORTNO);	// change to network byte
	
	int opt = 1;		// used in setsockopt
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));	// bind error
	
	// bind socket_fd with server_addr
	if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		printf("Server : Can't bind local address\n");		// error
		return;
	}
	
	listen(socket_fd, 5);		// set to accept to request
	
	
	/* setting semaphore */
	if ((semid = semget((key_t)PORTNO, 1, IPC_CREAT|0666)) == -1) {
		perror("semget failed");
		exit(1);
	}
	
	arg.val = 1;
	if ((semctl(semid, 0, SETVAL, arg)) == -1) {
		perror("semctl failed");
		exit(1);
	}

	while(1) {
		
		int status;
		pid = getpid();
		if ((pid = fork()) < 0){	// fork error
			printf("fork error \n");
			return;
		}
		
		else if (pid == 0) {	// fork to set child process
			web_pid = getpid();
			repeat(semid);	// go to repeat function
			return;
		}
		if ((pid = waitpid(pid, &status, 0)) < 0)	// wait for child process
			fprintf(stderr, "waitpid error\n");
		
		close(client_fd);
		
		sub_p++;		// count up the web connect (sub process)
		
		//printf("\nsub_p : %d \n", sub_p);
	}
	close(socket_fd);	// close socket
	return;
}

////////////////////////////////////////////////////////////////////////////
// main									  //
////////////////////////////////////////////////////////////////////////////
void main() {
	// get start time
	time_t start;
        struct tm *gtp;
        time(&start);
	
        gtp = gmtime(&start);   // set in gmtime
        start_t = mktime(gtp);           // t1 : starting time by gmtime
	
	signal(SIGALRM, sigalrm_handler);	// when alarm signal occured
	
	// setting home as '~/cache'
	getHomeDir(home);
	strcat(home, "/cache");
	mkdir(home, S_IRWXU | S_IRWXG | S_IRWXO);	// make cache directory
	
	// setting home as '~/logfile/logfile.txt'
	getHomeDir(home);
	strcat(home, "/logfile");
	mkdir(home, S_IRWXU | S_IRWXG | S_IRWXO);	// make logfile directory
	strcat(home, "/logfile.txt");
	fp_log = fopen(home, "w");			// fp_log : logfile's file pointer
	
	sys_fork();
}
