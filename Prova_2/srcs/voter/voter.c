/*
	STUDENTE: Emanuele Di Maio
	MATRICOLA: N4600****

*/

#include <asm/io.h>

//Aggiungere header file necessari
#include <asm/rtai.h>
#include <rtai_shm.h>
#include <rtai_sched.h>
#include <rtai_sem.h>

#include <rtai_mbx.h>
#include <rtai_shm.h>
#include <rtai_sem.h>
#include <rtai_msg.h>
#include "parameters.h"

//definizioni variabili globali e puntatori alle shared memory
static RT_TASK t_speed;
static RT_TASK t_altitude;
static RT_TASK t_temperature;
static struct raw_sensors_data *rsd;
static struct processed_sensors_data *psd;
static struct shm_stop_task *sst;

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

static MBX* ape_req;

static int enable_altitude = 1;
module_param(enable_altitude, int, 0664);

static int enable_speed = 1;
module_param(enable_speed, int, 0664);

static int enable_temperature = 1;
module_param(enable_temperature, int, 0664);

//implementazione funzioni dei task per temperature, speed, altitude
static void calcolaAltitudine(int t){
	unsigned int altitudine_buffer[ALTITUDE_SIZE];
	unsigned int altitudine_corretta=0;
	int anomalia=0;
	int msg=3;
	long long slack=0;
	int overrun=0;
	int j, i, found;
	int running=1;
	while(running){
		anomalia=0;			
		//Faccio wait-signal sul semaforo per acquisire da rsd
		rt_sem_wait(sem_rsd_alt);

		for(i=0; i<ALTITUDE_SIZE; i++){
			altitudine_buffer[i] = rsd->altitudes[i]; //Copio in un buffer locale
		}
		rt_sem_signal(sem_rsd_alt);

		j=0;
		i=0;
		found = 0;
		while(i<ALTITUDE_SIZE-1 && (found != 2)){ //Siccome 3 sono sicuramente corretti (e uguali), incremento il found finché non trovo il valore corretto
			for(j=i+1; j<ALTITUDE_SIZE; j++){
				if(altitudine_buffer[i] != 0){
					if(altitudine_buffer[i] == altitudine_buffer[j]){ //Scorro i valori
						altitudine_corretta = altitudine_buffer[i];
						found += 1; //Incremento il found. Al secondo incremento (matching di 3 valori uguali) esco dal ciclo
					}	
				}
			}
			i++;
		}
		for(i=0; i<ALTITUDE_SIZE; i++){
			if(altitudine_corretta != altitudine_buffer[i]){ //Controllo se c'è l'anomalia
				anomalia = 1;
			}
		}

		if(anomalia == 1){
			rt_mbx_send(ape_req,&msg,sizeof(int));
		}
		//Faccio wait-signal sul semaforo per scrivere da psd
		rt_sem_wait(sem_psd_alt); //Fanno bloccare tutto????
		psd->altitude = altitudine_corretta;
		rt_sem_signal(sem_psd_alt);
		
		rt_sem_wait(sem_stop_alt);
		if(sst->stop_alt == 3){
			running = 0;
			rt_sem_wait(sem_psd_alt);	
			psd->altitude = -1;
			rt_sem_signal(sem_psd_alt);
		}
		rt_sem_signal(sem_stop_alt);		

		slack = count2nano(next_period() - rt_get_time()); //Slack
		overrun = rt_task_wait_period();
		if(overrun == RTE_TMROVRN || slack < 0){ //Overrun
			rt_printk("Altitudine in overrun con slack: %lld\n", slack);
		}
	}
}

static void calcolaVelocita(int t){
	unsigned int velocita_buffer[SPEED_SIZE];
	unsigned int velocita_corretta=0;	
	int j, i;
	int found;
	int anomalia=0;
	int msg=2;
	long long slack=0;
	int overrun=0;
	int running = 1;
	while(running){

		anomalia = 0;
		//Faccio wait-signal sul semaforo per acquisire da rsd
		rt_sem_wait(sem_rsd_speed);

		for(i=0; i<SPEED_SIZE; i++){
			velocita_buffer[i] = rsd->speeds[i]; //Copia in un buffer locale			
		}
		rt_sem_signal(sem_rsd_speed);

		j = 0;
		i = 0;
		found = 0;
		while(i<SPEED_SIZE-1 && !found){ //Appena trovo il secondo valore uguale (perché 2 saranno sicuramente giusti), esco. Oppure con l'ultimo confronto i-esimo-1 e j-esimo
			for(j=i+1; j<SPEED_SIZE; j++){
				if(velocita_buffer[i] == velocita_buffer[j]){ //Matching di due valori uguali
					velocita_corretta = velocita_buffer[i];
					found = 1;
				}	
			}
			i++;
		}
		for(i=0; i<SPEED_SIZE; i++){
			if(velocita_corretta != velocita_buffer[i]) //Se esiste un valore diverso da quello corretto, c'è un'anomalia
				anomalia=1;
		}

		if(anomalia == 1){
			rt_mbx_send(ape_req,&msg,sizeof(int));
		}
		//Faccio wait-signal sul semaforo per scrivere da psd
		rt_sem_wait(sem_psd_speed);
		psd->speed = velocita_corretta;
		rt_sem_signal(sem_psd_speed);

		rt_sem_wait(sem_stop_speed);
		if(sst->stop_speed == 2){
			running = 0;
			rt_sem_wait(sem_psd_speed);	
			psd->speed = -1;
			rt_sem_signal(sem_psd_speed);
		}
		rt_sem_signal(sem_stop_speed);

		slack = count2nano(next_period() - rt_get_time()); //Slack
		overrun = rt_task_wait_period();
			if(overrun == RTE_TMROVRN || slack<0){
			rt_printk("Velocita in overrun con slack: %lld\n", slack);
		}
	}
}

static void calcolaTemperatura(int t){
	unsigned int temperatura_buffer[TEMP_SIZE];
	unsigned int temperatura_corretta=0;
	int anomalia=0;
	int msg=1;
	long long slack=0;
	int overrun=0;
	int i;
	int running = 1;
	while(running){
		anomalia = 0;
		//Faccio wait-signal sul semaforo per acuqisire da rsd
		rt_sem_wait(sem_rsd_temp);
		for(i=0; i<TEMP_SIZE; i++){
			temperatura_buffer[i] = rsd->temperatures[i]; //Copia in locale
		}
		rt_sem_signal(sem_rsd_temp);

		for(i=0; i<TEMP_SIZE; i++){
			if(temperatura_buffer[i] != 0){ //Se ha lo zero, scartalo. Le altre 2 sono sempre corrette
				temperatura_corretta = temperatura_buffer[i];
			}
			else{
				anomalia=1;
			}
		}

		if(anomalia == 1){
			rt_mbx_send(ape_req,&msg,sizeof(int));
		}
		//Faccio wait-signal sul semaforo per acquisire da psd
		rt_sem_wait(sem_psd_temp);
		psd->temperature = temperatura_corretta;
		rt_sem_signal(sem_psd_temp);

		rt_sem_wait(sem_stop_temp);
		if(sst->stop_temp == 1){
			running = 0;
			rt_sem_wait(sem_psd_temp);	
			psd->temperature = -1;
			rt_sem_signal(sem_psd_temp);
		}
		rt_sem_signal(sem_stop_temp);

		slack = count2nano(next_period() - rt_get_time()); //slack
		overrun = rt_task_wait_period();
		if(overrun == RTE_TMROVRN || slack<0){
			rt_printk("Temperatura in overrun con slack: %lld\n", slack);
		}
	
	}
}



int init_module(void)
{
	RTIME tick_period;
	RTIME start_time;
	//inizializzazione task
	if(enable_altitude) rt_task_init_cpuid(&t_altitude, (void *)calcolaAltitudine, 0, STACK_SIZE, 1, 0, 0, 0);
	if(enable_speed) rt_task_init_cpuid(&t_speed, (void *)calcolaVelocita, 0, STACK_SIZE, 1, 0, 0, 0);
	if(enable_temperature) rt_task_init_cpuid(&t_temperature, (void *)calcolaTemperatura, 0, STACK_SIZE, 1, 0, 0, 0);

	//allocazione shared memory
	rsd = rtai_kmalloc(RAW_SEN_SHM, sizeof(struct raw_sensors_data));
	psd = rtai_kmalloc(PROC_SEN_SHM, sizeof(struct processed_sensors_data));
	sst = rtai_kmalloc(SHM_STOP_TASK, sizeof(struct shm_stop_task));
	sst->stop_temp = 0;
	sst->stop_alt = 0;
	sst->stop_speed = 0;

	//inizializzazione coda di messaggi
	ape_req = rt_typed_named_mbx_init(MAILBOX_REQ_ID,MAILBOX_SIZE,FIFO_Q);

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

	//avvio task periodici
	tick_period = nano2count(TICK_TIME);
	start_time = rt_get_time() + tick_period;

	if(enable_altitude) rt_task_make_periodic(&t_altitude, start_time, tick_period);
	if(enable_speed) rt_task_make_periodic(&t_speed, start_time, tick_period*2);
	if(enable_temperature) rt_task_make_periodic(&t_temperature, start_time, tick_period*4);

	//scheduling con rate monotonic
	rt_spv_RMS(0);

	return 0;
}


void cleanup_module(void)
{	
	//rimozione task e shared memory
	rt_task_delete(&t_altitude);
	rt_task_delete(&t_speed);
	rt_task_delete(&t_temperature); 

	rt_named_sem_delete(sem_rsd_temp); //DISTRUZIONE DEI SEMAFORI
	rt_named_sem_delete(sem_rsd_alt);
	rt_named_sem_delete(sem_rsd_speed);
	rt_named_sem_delete(sem_psd_temp);
	rt_named_sem_delete(sem_psd_alt);
	rt_named_sem_delete(sem_psd_speed);
	rt_named_sem_delete(sem_stop_temp);
	rt_named_sem_delete(sem_stop_alt);
	rt_named_sem_delete(sem_stop_speed);


	rt_named_mbx_delete(ape_req);

	rtai_kfree(PROC_SEN_SHM);
	rtai_kfree(RAW_SEN_SHM);
	rtai_kfree(SHM_STOP_TASK);
	return;
}
