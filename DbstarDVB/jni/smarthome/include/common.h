//全局使用的头文件，不允许与其他文件存在依赖

#ifndef __COMMON_H__
#define __COMMON_H__
#include <errno.h>

#define DEBUG(x...) do{printf("[%s:%d] ", __FUNCTION__, __LINE__);printf(x);}while(0)

#define ERROROUT(ERRSTR...) \
			do{printf("[%s:%s:%d] ", __FILE__, __FUNCTION__, __LINE__);\
			if(errno!=0) printf("[err note: %s]",strerror(errno));\
			printf(ERRSTR);}while(0)

#define MIN_LOCAL(a,b) ((a)>(b)?(b):(a))

typedef enum{
	BOOL_FALSE = 0,
	BOOL_TRUE
}BOOL_E;

#define MAXCMD (5)									//max cmd/msg number
#define MAXLEN (8192)									//the size of the transmission
#define MAXSLEEP (128)									//the max sleep time of reconnect

#define WORKSPACE_SURFIX	"/data/dbstar/smarthome/"
#define DATABASE_DIR 		WORKSPACE_SURFIX"database/"
#define DATABASE			"/data/dbstar/Smarthome.db"	//sqlite3 database
#define SETTING_BASE		WORKSPACE_SURFIX"settings/base.ini"	//serverconfig path on ppc
#define SQLITECMDLEN (512)							//the max number byte contained in "cmdStr"---command string--sqlite.cpp
#define WAITRES	(600)									//wait 600*myDelay(5) ms, if no response, return "#ff ff ff ffff#ff#ff ff#ff#"
#define SYNCTIME (10)									//sync time
#define CMD_SIZE		(8192)
#define ALTERABLE_ENTITY_SIZE	(8192)

#define SN_DEFAULT_TEST			"72392061919421673185"		//WARNING: only for test, the lenght of sn is 20 currently
#define SW_VERSION				"1.2.1"
#define SMARTPOWER_SERVER_IP		"211.160.203.86"		// the ip of baidu.com is "61.135.169.105"
#define	SMARTPOWER_SERVER_PORT		(9999)
#define	SMARTLIFE_SERVER_PORT		(9103)		// for smarthome apk

#define PREFIX_REGIST				"#rs#"
#define PREFIX_EXCEPTION_RESONSE	"#ff#"
#define PREFIX_REBOOT				"#reboot#"
#define PREFIX_DOWN_CMD				"#tt#"
#define PREFIX_UP_CMD				"#cc#"
#define PREFIX_ACTIVE_REPORT		"#re#"
#define PREFIX_ALARM				"#am#"		// in document, this is "#arm#"

#define CMD_KEYWORD_SYNC			"sync"
#define SMART_POWER_CMD_NUM			(32)

#define FIFO_STR_SIZE				(128)
#define FIFO_DIR					WORKSPACE_SURFIX"fifo/"
#define FIFO_2_SOCKET				FIFO_DIR"fifo_2_socket"
#define FIFO_2_INSTRUCTION			FIFO_DIR"fifo_2_instruction"
#define FIFO_SOCKET_SELF			FIFO_DIR"fifo_socket_self"

#define DEV_TTY_0		"/dev/ttyS0"
#define DEV_TTY_1		"/dev/ttyS1"
#define DEV_TTY_2		"/dev/ttyS2"
#define DEV_TTY_3		"/dev/ttyS3"

#define SERIAL_RESPONSE_LEN_MIN	(12)

// 68 a0 a1 a2 a3 a4 a5 68 01 02 43 C3 cs 16
// 发往串口的命令，长度至少包括：一个开始符68，6个标识socket id，socket id结束符68，校验符cs，指令结束符16
#define SERIAL_CMD_SEND_LEN_MIN	(10)
#define SERIAL_CMD_SEND_LEN_MAX	(64)

typedef enum{
	SOCKET_STATUS_EXCEPTION = -1,
	SOCKET_STATUS_CLOSED,
	SOCKET_STATUS_CREATE,
	SOCKET_STATUS_DISCONNECT,
	SOCKET_STATUS_CONNECTED,
	SOCKET_STATUS_REGISTING,
	SOCKET_STATUS_REGISTED,
	SOCKET_STATUS_UNREGIST
}SOCKET_STATUS_E;

typedef enum{
	CMD_STATUS_NULL = 0,
	CMD_STATUS_WRITING = 1,
	CMD_STATUS_WRITED = 64,
	CMD_STATUS_PROCESSING = 65,
	CMD_STATUS_PROCESSED = 129,
	CMD_STATUS_READING,
	CMD_STATUS_READED
}CMD_STATUS_E;

typedef enum{
	CMD_HEADER_UNDEFINED = -1,
	CMD_HEADER_INVALID = 0,
	CMD_HEADER_REGIST,
	CMD_HEADER_ISSUE,
	CMD_HEADER_REPORTED,
	CMD_HEADER_ALARM,
	CMD_HEADER_REBOOT,
	CMD_HEADER_SYNC,
	CMD_HEADER_SYNC_OK,
	CMD_HEADER_SYNC_FAILD,
	CMD_SYNC_DEVS,
	CMD_SYNC_MODL,
	CMD_SYNC_TIME,
	CMD_HEADER_INNER,
	CMD_ACTIVE_REPORTED_ACTPOWER,
	CMD_ACTIVE_REPORTED_POWER,
	CMD_ACTIVE_REPORTED_STATUS,
	CMD_INVALID
}CMD_HEADER_E;

// If CMD_ENTITY_MODEL_ARRAY, entity[0,1] means the model num. 
// So, it must has its largest num, perhaps 16 ??.
typedef enum{
	CMD_ENTITY_NORMAL_STR = 0,
	CMD_ENTITY_MODEL_ARRAY
}CMD_ENTITY_TYPE_E;

typedef struct{
	int type_id;
	int cmd_type;
	int control_val;
	int control_time;
	int frequency;
	char remark[64];
	int model_id;
}MODEL_S;

typedef struct{
	CMD_STATUS_E		status;	//空白？接收完毕？处理完毕？
	unsigned int		id;		//通常情况下各条命令具有唯一id，但同步指令？
	CMD_HEADER_E		type;
	unsigned int		send_try;
	char				serv_str[128];
	char				entity[CMD_SIZE];
	CMD_ENTITY_TYPE_E	entity_type;
}SMART_POWER_CMD_S;

typedef enum{
	INSTRUCTION_UNDEFINED = 0,
	INSTRUCTION_CTRL,		// make sure this value is 1, i.e. "01"
	INSTRUCTION_INQUIRE,	// make sure this value is 2, i.e. "02"
	INSTRUCTION_OP,			// make sure this value is 3, i.e. "03"
	INSTRUCTION_OTHER		// make sure this value is 4, i.e. "04"
}INSTRUCTION_TYPE_E;


typedef struct{
	int 				type_id;		// actual using 6B
	char				reserve[5];		// actual using 4B
	INSTRUCTION_TYPE_E	type;
	int					arg1;			// parhaps need char[3] for two chars and one '\n'
	int					arg2;			// parhaps need char[3] for two chars and one '\n'
	int					alterable_flag;	// parhaps need char[3] for two chars and one '\n'
	char				alterable_entity[ALTERABLE_ENTITY_SIZE];
	int					insert_flag;
	int					index_in_cmds;
}INSTRUCTION_S;

typedef struct{
	time_t	server_time;
	double	difference_time;	// server_time - stb_time
}SMART_POWER_TIME_S;



typedef enum{
	CMD_ARRAY_OP_R,
	CMD_ARRAY_OP_PROCESS,
	CMD_ARRAY_OP_W
}CMD_ARRAY_OP_E;

typedef enum{
	ERR_TIMEOUT = -8,
	ERR_FORMAT = -7,
	ERR_FORMATPRO = -6,
	ERR_SOCKET = -5,
	ERR_DATABASE = -4,
	ERR_MEMORY = -3,
	ERR_SERIAL = -2,
	ERR_OTHER = -1,			//	usually, it means "&ff"
	RESULT_OK = 0,			// usually, it means "&00"
	RESULT_ALTERABLE_ENTITY_FILL_OK = 1		//	usually, it means the alterable_entity is filled
}INSTRUCTION_RESULT_E;

typedef struct{
	int		type_id;
	int		location_id;
	int		icon_id;
	int		oper_id;
	char	socket_id[17];
	char	room_name[9];
	char	dev_name[41];
	double	power_cumulation;
	int 	power_inquire_status;
	int		socket_status;
	int		first_equipment;
	int		active_inquire_status;
}EQUIPMENT_S;

#define EQUIPMENT_NUM	256
#define SERIAL_CMD_SIZE	256


typedef enum{
	SMART_SOCKET_ACTION_UNDEFINED = -1,
	SMART_SOCKET_ACTIVE_POWER_CONSUMPTION_READ = 0,	// 有功总电量
	SMART_SOCKET_VOLTAGE_READ,						// 电压
	SMART_SOCKET_POWER_CURRENT_READ,				// 电流
	SMART_SOCKET_ACTIVE_POWER_READ,					// 有功功率
	SMART_SOCKET_REACTIVE_POWER_READ,				// 无功功率
	SMART_SOCKET_POWER_COEFFICIENT_READ,			// 功率因数
	SMART_SOCKET_RELAY_DISCONNECT,					// 继电器断开
	SMART_SOCKET_RELAY_CONNECT,						// 继电器闭合
	SMART_SOCKET_RELAY_STATUS_READ,					// 继电器状态
	SMART_SOCKET_ADDR_CONFIRM,						// 地址确认
	SMART_SOCKET_INSTRUCTION_INVALID,				// 指令错误
	SMART_SOCKET_COMMUNICATION_FAILD,				// 通讯失败
	
	CURTAIN_GOAHEAD = 20,						// 窗帘前进
	CURTAIN_GOBACK,								// 窗帘后退
	CURTAIN_STOP,								// 窗帘停止
	CURTAIN_CONFIRM								// 窗帘地址验证
}SMART_SOCKET_ACTION_E;

typedef enum{
	SMART_SOCKET_RELAY_STATUS_OFF = 0,
	SMART_SOCKET_RELAY_STATUS_ON,
	SMART_SOCKET_RELAY_STATUS_UNKNOWN
}SMART_SOCKET_RALAY_STATUS_E;

// the length of msg string can not loong than 32
#define MSGSTR_INSTRUCTION_SELF		"msgstr_instruction_self"
#define MSGSTR_2_INSTRUCTION		"msgstr_2_instruction"
#define MSGSTR_SYNC_2_INSTRUCTION	"msgstr_sync_2_instruction"
#define MSGSTR_2_SOCKET				"msgstr_2_socket"
#define MSGSTR_SOCKET_SELF			"msgstr_socket_self"

#define INSTRUCTION_INSERT_NUM (64)


typedef enum{
	TIMER_TYPE_MANUAL = 0,	// 全手工控制，由unrigist销毁；
	TIMER_TYPE_INNER,		// 内部自动实现的，不用注册到timer数组中。1、每到*5分钟时查询功率；2、每到50分钟时查询电量；3、每到55——00时上报功率；4、每到00——05时上报功率；5、每到零时刷timing task
	TIMER_TYPE_TASK			// 服务器下发的定时任务，包括定时模式任务在内。每到零时进行刷新
}TIMER_TYPE_E;

#define WORK_NORMAL_NOT_TEST	// 正常运行程序应当放开此宏
//#define TEST_SERIAL_CMD_ONLY_ONCE	// 仅在调试串口时使用，每次执行可执行程序，只对串口操作一个write-read组合
//#define HEARTBEAT_SUPPORT		// 是否发送心跳。目前暂不发送

#define SERIAL_RECV_RETRY		(16)	// 从串口接收命令尝试的次数，这个值和select的超时时间共同决定了串口接收时的反应速度
#define TIMEZONE_EMENDATION		(8)	// 北京时间，在0时区时间基础上加8个小时。

int appoint_str2int(char *str, unsigned int str_len, unsigned int start_position, unsigned int appoint_len, int base);
void myDelay(int delay);

void ms_sleep(unsigned int ms);
unsigned int randint(float rand_top);
int timezone_repair(void);
int dir_exist_ensure(char *dir);
time_t zero_sec_get(time_t appoint_secs);
time_t sec_from_0_at_day(time_t appoint_secs);
time_t time_get(time_t *timer);
int print_localtime_sec2str(time_t your_sec);

#endif

