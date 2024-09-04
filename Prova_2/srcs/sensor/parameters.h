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