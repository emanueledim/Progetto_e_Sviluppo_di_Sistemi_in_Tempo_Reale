//---------------- PARAMETERS.H ----------------------- 

#define TICK_TIME 250000000

#define STACK_SIZE 10000

#define ALTITUDE_SIZE 5
#define SPEED_SIZE 3
#define TEMP_SIZE 3

#define RAW_SEN_SHM 121111
#define PROC_SEN_SHM 121112

#define SEM_RSD_TEMP_ID "sem_rsd_temp_id"
#define SEM_RSD_ALT_ID "sem_rsd_alt_id"
#define SEM_RSD_SPEED_ID "sem_rsd_speed_id"
#define SEM_PSD_TEMP_ID "sem_psd_temp_id"
#define SEM_PSD_ALT_ID "sem_psd_alt_id"
#define SEM_PSD_SPEED_ID "sem_psd_speed_id"

#define MAILBOX_REQ_ID "mbx_ape_req"
#define MAILBOX_SIZE 256

#define SHM_STOP_TASK 125000

#define SEM_STOP_TEMP "sem_stop_temp"
#define SEM_STOP_ALT "sem_stop_alt"
#define SEM_STOP_SPEED "sem_stop_speed"



struct shm_stop_task{
	int stop_temp;
	int stop_alt;
	int stop_speed;
};

struct raw_sensors_data
{
    unsigned int altitudes[ALTITUDE_SIZE]; 	//uptated every 250ms
    unsigned int speeds[SPEED_SIZE];		//updated every 500ms
    int temperatures[TEMP_SIZE];		//uptaded every	second
};

struct processed_sensors_data
{
    unsigned int altitude;
    unsigned int speed;
    int temperature;
};