//-------------MONITOR.C----------------
/*
	STUDENTE: Emanuele Di Maio
	MATRICOLA: N4600****

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <rtai_lxrt.h>
#include <rtai_shm.h>
#include <rtai_mbx.h>
#include <rtai_msg.h>
#include <sys/io.h>
#include <signal.h>

#include "parameters.h"


static RT_TASK *main_task;
static RT_TASK *tbs_task; 
static RT_TASK *temp_task; 
static RT_TASK *alt_task; 
static RT_TASK *speed_task; 
static RT_TASK *buddy_task;

static int keep_on_running = 1;

static pthread_t thread_main_tbs; 
static pthread_t thread_temp_ape; 
static pthread_t thread_alt_ape; 
static pthread_t thread_speed_ape; 
static pthread_t thread_buddy;

static MBX* ape_req;

//definizioni semafori
static SEM *sem_rsd_temp;
static SEM *sem_rsd_alt;
static SEM *sem_rsd_speed;
static SEM *sem_psd_temp;
static SEM *sem_psd_alt;
static SEM *sem_psd_speed;
static SEM *sem_stop_temp;
static SEM *sem_stop_alt;
static SEM *sem_stop_speed;

static struct raw_sensors_data *rsd;
static struct processed_sensors_data *psd;
static struct shm_stop_task *sst;

static void endme(int dummy) {keep_on_running = 0;}
static RTIME deadline_precedente;

/*
ASSUNZIONI TEMPI DI CALCOLO DEI TASK APERIODICI:
	Alt: C = 200ms
	Temp: C = 100ms
	Speed: C = 100ms
	TICK_TIME = 100ms

BANDA TBS:
	Utbs = 50% = 0.5
*/

static void *temp_ape(void *p) {
	int proc_temp_prelevata;
	int raw_temp_prelevata[TEMP_SIZE];
	int i;
	int msg_received=0;
	RTIME now;
	RTIME tick = nano2count(TICK_TIME);
	int count_temp = 0;
	int max = 0;
	int deadline = 0;

	if (!(temp_task = rt_task_init_schmod(nam2num("TMPAPE"), 2, 0, 0, SCHED_FIFO, 0x1))) {
		printf("ERRORE INIZIALIZZAZIONE TEMP APERIODIC TASK\n");
		exit(1);
	}

	rt_make_hard_real_time();

	while(keep_on_running){
		rt_receive(tbs_task, &msg_received);  //Riceve 1 dal task tbs

		//Modifica della deadline
		now = rt_get_time(); //POTEVO METTERE IL CALCOLO DIRETTAMENTE NEL TBS E PASSARLO COME MESSAGGIO
		if(now > deadline_precedente) max = now;
		else max = deadline_precedente;
		deadline = max+tick/Utbs;
		rt_task_set_resume_end_times(now, deadline);
		deadline_precedente = deadline;

		rt_sem_wait(sem_rsd_temp); //Wait
		for(i=0; i<TEMP_SIZE; i++){
			raw_temp_prelevata[i] = rsd->temperatures[i];
		}
		rt_sem_signal(sem_rsd_temp); //Signal
		rt_sem_wait(sem_psd_temp); //Wait
		proc_temp_prelevata = psd->temperature;
		rt_sem_signal(sem_psd_temp);//Signal
	
		for(i=0; i<TEMP_SIZE; i++)		
			rt_send(buddy_task, raw_temp_prelevata[i]); //Send diretta al task buddy
		
		rt_send(buddy_task, proc_temp_prelevata); //Send diretta al task buddy
		
		count_temp++;
		if(count_temp == 10){
			rt_sem_wait(sem_stop_temp);
			sst->stop_temp = 1;
			rt_sem_signal(sem_stop_temp);
		}
	}
	rt_task_delete(temp_task);
	return 0;
}

static void *alt_ape(void *p) {
	unsigned int proc_alt_prelevata;
	unsigned int raw_alt_prelevata[ALTITUDE_SIZE];
	int i;
	int msg_received=0;
	RTIME now;
	RTIME tick = nano2count(TICK_TIME);
	int count_alt = 0;
	int max = 0;
	int deadline = 0;

	if (!(alt_task = rt_task_init_schmod(nam2num("ALTAPE"), 2, 0, 0, SCHED_FIFO, 0x1))) {
		printf("ERRORE INIZIALIZZAZIONE ALTITUDE APERIODIC TASK\n");
		exit(1);
	}

	rt_make_hard_real_time();

	while(keep_on_running){
		rt_receive(tbs_task, &msg_received);  //Riceve 3 dal task tbs

		//Modifica della deadline
		now = rt_get_time();
		if(now > deadline_precedente) max = now;
		else max = deadline_precedente;
		deadline = max+((tick*2)/Utbs);
		rt_task_set_resume_end_times(now, deadline);
		deadline_precedente = deadline;

		rt_sem_wait(sem_rsd_alt); //Wait
		for(i=0; i<ALTITUDE_SIZE; i++){
			raw_alt_prelevata[i] = rsd->altitudes[i];
		}
		rt_sem_signal(sem_rsd_alt); //Signal
		rt_sem_wait(sem_psd_alt); //Wait
		proc_alt_prelevata = psd->altitude;
		rt_sem_signal(sem_psd_alt);//Signal
	
		for(i=0; i<ALTITUDE_SIZE; i++)		
			rt_send(buddy_task, raw_alt_prelevata[i]); //Send diretta al task buddy
		
		rt_send(buddy_task, proc_alt_prelevata); //Send diretta al task buddy
		
		count_alt++;
		if(count_alt == 20){
			rt_sem_wait(sem_stop_alt);
			sst->stop_alt = 3;
			rt_sem_signal(sem_stop_alt);
		}
	}
	rt_task_delete(alt_task);

	return 0;
}

static void *speed_ape(void *p) {

	unsigned int proc_speed_prelevata;
	unsigned int raw_speed_prelevata[SPEED_SIZE];
	int i;
	int msg_received=0;
	RTIME now;
	RTIME tick = nano2count(TICK_TIME);
	int count_speed = 0;
	int max = 0;
	int deadline = 0;

	if (!(speed_task = rt_task_init_schmod(nam2num("SPDAPE"), 2, 0, 0, SCHED_FIFO, 0x1))) {
		printf("ERRORE INIZIALIZZAZIONE SPEED APERIODIC TASK\n");
		exit(1);
	}

	rt_make_hard_real_time();

	while(keep_on_running){
		rt_receive(tbs_task, &msg_received); //Riceve 2 dal task tbs

		//Modifica della deadline
		now = rt_get_time();
		if(now > deadline_precedente) max = now;
		else max = deadline_precedente;
		deadline = max+(tick/Utbs);
		rt_task_set_resume_end_times(now, deadline);
		deadline_precedente = deadline;

		rt_sem_wait(sem_rsd_speed); //Wait
		for(i=0; i<SPEED_SIZE; i++){
			raw_speed_prelevata[i] = rsd->speeds[i];
		}
		rt_sem_signal(sem_rsd_speed); //Signal
		rt_sem_wait(sem_psd_speed); //Wait
		proc_speed_prelevata = psd->speed;
		rt_sem_signal(sem_psd_speed);//Signal
	
		for(i=0; i<SPEED_SIZE; i++)		
			rt_send(buddy_task, raw_speed_prelevata[i]); //Send diretta al task buddy
		
		rt_send(buddy_task, proc_speed_prelevata); //Send diretta al task buddy
		
		count_speed++;
		if(count_speed == 10){
			rt_sem_wait(sem_stop_speed);
			sst->stop_speed = 2;
			rt_sem_signal(sem_stop_speed);
		}
	}

	rt_task_delete(speed_task);
	return 0;
}


static void *tbs_func(void *p) {
	int messaggio_ricevuto = 0;

	if (!( tbs_task = rt_task_init_schmod(nam2num("TBSTSK"), 1, 0, 0, SCHED_FIFO, 0x1))) {
		printf("ERRORE INIZIALIZZAZIONE TBS TASK\n");
		exit(1);
	}

	rt_make_hard_real_time();

	while (keep_on_running)
	{
		rt_mbx_receive(ape_req, &messaggio_ricevuto, sizeof(int)); //RICEVO QUALCOSA
		if(messaggio_ricevuto == 1) //Temperatura
		{
			rt_send(temp_task, messaggio_ricevuto);	//SEND DIRETTE
		}
		if(messaggio_ricevuto == 2) //Velocità
		{
			rt_send(speed_task, messaggio_ricevuto);	
		}
		if(messaggio_ricevuto == 3) //Altitudine
		{
			rt_send(alt_task, messaggio_ricevuto);	
		}
	}
	rt_task_delete(tbs_task);
	return 0;
}

static void *buddy(void *p){
	int temp_ric;
	unsigned int alt_ric;
	unsigned int speed_ric;
	int i=0;

	if (!(buddy_task = rt_task_init_schmod(nam2num("BUDDY"), 3, 0, 0, SCHED_FIFO, 0x1))) {
		printf("ERRORE INIZIALIZZAZIONE SPEED APERIODIC TASK\n");
		exit(1);
	}
	
	rt_make_soft_real_time(); 

	while(keep_on_running){
		if(rt_receive_if(temp_task, &temp_ric)){
			printf("Warning task TEMP: %d ", temp_ric);
			for(i=1; i<TEMP_SIZE-2; i++){
				rt_receive_if(temp_task, &temp_ric);	
				printf("%d ", temp_ric);	
			}
			if(rt_receive_if(temp_task, &temp_ric)) printf("-> %d\n", temp_ric);
		}

		if(rt_receive_if(alt_task, &alt_ric)){
			printf("Warning task ALT: %u ", alt_ric);
			for(i=1; i<ALTITUDE_SIZE-2; i++){
				rt_receive_if(alt_task, &alt_ric);			
				printf("%u ", alt_ric);	
			}
			if(rt_receive_if(alt_task, &alt_ric)) printf("-> %u\n", alt_ric);
		}

		if(rt_receive_if(speed_task, &speed_ric)){
			printf("Warning task SPEED: %u ", speed_ric);
			for(i=1; i<SPEED_SIZE-2; i++){
				rt_receive_if(speed_task, &speed_ric);			
				printf("%u ", speed_ric);	
			}
			if(rt_receive_if(speed_task, &speed_ric)) printf("-> %u\n", speed_ric);
		}
	}
	return 0;
}

int main(void)
{
	deadline_precedente = 0;
 	signal(SIGQUIT, endme);

	if (!(main_task = rt_task_init_schmod(nam2num("MAINTS"), 0, 0, 0, SCHED_FIFO, 0x1))) {
		printf("ERRORE INIIZIALIZZAZIONE MAIN TASK\n");
		exit(1);
	}
	
	//allocazione shared memory
	rsd = rtai_malloc(RAW_SEN_SHM, sizeof(struct raw_sensors_data)); 
	psd = rtai_malloc(PROC_SEN_SHM, sizeof(struct processed_sensors_data));
	sst = rtai_malloc(SHM_STOP_TASK, sizeof(struct shm_stop_task)); //SHM DI STOP

	//inizializzazione mbx
	ape_req = rt_typed_named_mbx_init(MAILBOX_REQ_ID, MAILBOX_SIZE, FIFO_Q);

	//inizializzazione semafori
	sem_rsd_temp = rt_typed_named_sem_init(SEM_RSD_TEMP_ID, 1, BIN_SEM | FIFO_Q); //Semaforo per la shared memory raw sensor data temp
	sem_rsd_alt = rt_typed_named_sem_init(SEM_RSD_ALT_ID, 1, BIN_SEM | FIFO_Q); //Semaforo per la shared memory row sensor data alt
	sem_rsd_speed = rt_typed_named_sem_init(SEM_RSD_SPEED_ID, 1, BIN_SEM | FIFO_Q); //Semaforo per la shared memory raw sensor data speed
	sem_psd_temp = rt_typed_named_sem_init(SEM_PSD_TEMP_ID, 1, BIN_SEM | FIFO_Q); //Semaforo per la shared memory process sensor data temp
	sem_psd_alt = rt_typed_named_sem_init(SEM_PSD_ALT_ID, 1, BIN_SEM | FIFO_Q); //Semaforo per la shared memory process sensor data alt
	sem_psd_speed = rt_typed_named_sem_init(SEM_PSD_SPEED_ID, 1, BIN_SEM | FIFO_Q); //Semaforo per la shared memory process sensor data speed
	sem_stop_temp = rt_typed_named_sem_init(SEM_STOP_TEMP, 1, BIN_SEM | FIFO_Q);
	sem_stop_alt = rt_typed_named_sem_init(SEM_STOP_ALT, 1, BIN_SEM | FIFO_Q);
	sem_stop_speed = rt_typed_named_sem_init(SEM_STOP_SPEED, 1, BIN_SEM | FIFO_Q);
		
	pthread_create(&thread_main_tbs, NULL, tbs_func, NULL);
	pthread_create(&thread_temp_ape, NULL, temp_ape, NULL); 
	pthread_create(&thread_alt_ape, NULL, speed_ape, NULL); 
	pthread_create(&thread_speed_ape, NULL, alt_ape, NULL); 
	pthread_create(&thread_buddy, NULL, buddy, NULL);
		
	while (keep_on_running) { 
		rt_sleep(nano2count(750000000));		
	}


	rt_task_delete(main_task);
	rt_task_delete(tbs_task);
	rt_task_delete(temp_task);
	rt_task_delete(alt_task);
	rt_task_delete(speed_task);
	rt_task_delete(buddy_task);

	rt_named_sem_delete(sem_rsd_temp); 
	rt_named_sem_delete(sem_rsd_alt);
	rt_named_sem_delete(sem_rsd_speed);
	rt_named_sem_delete(sem_psd_temp);
	rt_named_sem_delete(sem_psd_alt);
	rt_named_sem_delete(sem_psd_speed);
	rt_named_sem_delete(sem_stop_temp);
	rt_named_sem_delete(sem_stop_alt);
	rt_named_sem_delete(sem_stop_speed);

	rt_named_mbx_delete(ape_req);

	rt_shm_free(RAW_SEN_SHM);
	rt_shm_free(PROC_SEN_SHM); 
	rt_shm_free(SHM_STOP_TASK);
	return 0;
}


