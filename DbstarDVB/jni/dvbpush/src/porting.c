﻿#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/statfs.h>
#include <sys/vfs.h>

#include "common.h"
#include "dvbpush_api.h"
#include "mid_push.h"
#include "softdmx.h"
#include "bootloader.h"
#include "xmlparser.h"
#include "sqlite.h"
#include "prodrm20.h"
#include "multicast.h"
#include "porting.h"
#include "drmapi.h"
#include "push.h"

#define INVALID_PRODUCTID_AT_ENTITLEINFO	(0)

static int 			s_settingInitFlag = 0;

static char			s_service_id[32];
static int			s_root_channel;
static char			s_root_push_file[256];
static unsigned int	s_root_push_file_size = 0;
static char			s_data_source[256];
static int			s_prog_data_pid = 0;

static char			s_dbstar_database_uri[256];
static char			s_smarthome_database_uri[256];
static int			s_debug_level = 0;
static char			s_xml[256];
static char			s_initialize_xml[256];
static char			s_column_res[256];
static int			s_software_check = 1;

static char			s_Language[64];
static char			s_serviceID[64];
static char			s_push_root_path[512];
static char 		*s_guidelist_unselect = NULL;

static char			s_jni_cmd_public_space[20480];

// 关于smart card的insert和remove标记是表示“曾经发生过……”，而不是现在一定是某个状态
static int			s_smart_card_insert_flag = 0;
static int			s_smart_card_remove_flag = 1;	// 当发生过拔卡事件时，此标记置1。为了应对插卡开机，初始化为1

static char			s_TestSpecialProductID[64];

static dvbpush_notify_t dvbpush_notify = NULL;

static int drm_time_convert(unsigned int drm_time, char *date_str, unsigned int date_str_size);
static int smartcard_entitleinfo_refresh();

/* define some general interface function here */

static void settingDefault_set(void)
{
	memset(s_service_id, 0, sizeof(s_service_id));
	memset(s_root_push_file, 0, sizeof(s_root_push_file));
	memset(s_data_source, 0, sizeof(s_data_source));
	
	memset(s_service_id, 0, sizeof(s_service_id));
	s_root_channel = ROOT_CHANNEL;
	strncpy(s_root_push_file, ROOT_PUSH_FILE, sizeof(s_root_push_file)-1);
	s_root_push_file_size = ROOT_PUSH_FILE_SIZE;
	
	s_prog_data_pid = PROG_DATA_PID_DF;
	
	snprintf(s_dbstar_database_uri, sizeof(s_dbstar_database_uri), "%s", DBSTAR_DATABASE);
	snprintf(s_smarthome_database_uri, sizeof(s_smarthome_database_uri), "%s", SMARTHOME_DATABASE);
	s_debug_level = 0;
	memset(s_xml, 0, sizeof(s_xml));
	snprintf(s_initialize_xml, sizeof(s_initialize_xml), "%d", INITIALIZE_XML);
	snprintf(s_column_res, sizeof(s_column_res), "%s", COLUMN_RES);
	s_software_check = 1;
	
	return;
}

/*
判断配置项（含有以separator（例如：“=”、“:”）分割的“item——value”组合的一行）是否合法，主要包括以下内容：
1、去除所有的不可显字符，只留下isgraph判断通过的；
2、如果是“#”打头则视为注释，不用解析；
3、有分隔符separator才算有效。
4、buf的数据可能会被改变，不可显字符都将去掉，且如果是有效的配置项，将会用'\0'替换“=”截断buf。
	所以如果需要原始字符串，则应在调用此函数前自行备份。

返回值：相当于配置项中separator后的位置，实际上是strchr(buf, '=')的返回值再加一（跳过“=”，指向value的地址）。
		因此，NULL表示此配置项无效，其他值则表示是value的指针
输出：	函数返回时，buf指向分隔符前的item
*/
char *setting_item_value(char *buf, unsigned int buf_len, char separator)
{
	if(NULL==buf || 0==buf_len || '#'==buf[0]){
		DEBUG("this line is ignored as explain\n");
		return NULL;
	}
//	DEBUG("read line: %s\n", buf);
	unsigned int i=0, j = 0;
	for(i=0; i<buf_len; i++){
#if 0
		if( (buf[i]>'0'&&buf[i]<'9')
			||(buf[i]>'A'&&buf[i]<'Z')
			||(buf[i]>'a'&&buf[i]<'z')
			||'_'==buf[i] || '-'==buf[i] || '@'==buf[i] )
#else
		if( isgraph(buf[i]) )		//or check it between 33('!') and 126('~')
#endif
		{
			if(j!=i)
				buf[j] = buf[i];
			j++;
		}
	}
	buf[j] = '\0';
//	DEBUG("buf: %s\n", buf);
	if('#'==buf[0]){
		DEBUG("ignore a line because of explain\n");
		return NULL;
	}
	char *p_value = strchr(buf, separator);
	if(p_value){
		*p_value = '\0';
		p_value ++;
		return p_value;
	}
	else
		return NULL;
}

/*
 push库会向push.conf配置的log目标目录写入日志，在异常情况下，日志日积月累过多会影响系统的正常运行。
 所以在开机时检查此目录的大小，大于一定值时删除
*/
// (33554432)==32*1024*1024	(8388608)==8*1024*1024
#define LIBPUSH_LOGDIR_SIZE	(8388608)
int libpush_logdir_check(void)
{
	FILE* fp;
	char tmp_buf[256];
	char *p_value;
	char push_log_dir[512];
	long long dir_size_total = 0LL;
	
	fp = fopen(PUSH_CONF,"r");
	if (NULL == fp)
	{
		ERROROUT("fopen %s faild!\n", PUSH_CONF);
	}
	else{
		DEBUG("fopen %s success\n", PUSH_CONF);
		memset(tmp_buf, 0, sizeof(tmp_buf));
		
		while(NULL!=fgets(tmp_buf, sizeof(tmp_buf), fp)){
			//DEBUG("[%s]\n", tmp_buf);
			p_value = setting_item_value(tmp_buf, strlen(tmp_buf), '=');
			if(NULL!=p_value)
			{
				DEBUG("setting item: %s, value: %s\n", tmp_buf, p_value);
				if(strlen(tmp_buf)>0 && strlen(p_value)>0){
					if(0==strcmp(tmp_buf, "LOG_DIR")){
						snprintf(push_log_dir,sizeof(push_log_dir),"%s/libpush",p_value);
						dir_size_total = dir_size(push_log_dir);
						DEBUG("size of %s is %lld\n", push_log_dir, dir_size_total);
						if(dir_size_total>=LIBPUSH_LOGDIR_SIZE){
							DEBUG("WARNING: log dir %s is too large, remove it\n", push_log_dir);
							remove_force(push_log_dir);
						}
						break;
					}
				}
			}
			memset(tmp_buf, 0, sizeof(tmp_buf));
		}
		fclose(fp);
	}
	DEBUG("check libpush logdir finish\n");
	
	return 0;
}

int setting_init(void)
{
	if(1==s_settingInitFlag){
		DEBUG("setting is init already\n");
		return 0;
	}
		
	FILE* fp;
	char tmp_buf[256];
	char *p_value;
	
	libpush_logdir_check();
	
	settingDefault_set();
	DEBUG("init settings with %s\n", SETTING_BASE);
	fp = fopen(SETTING_BASE,"r");
	if (NULL == fp)
	{
		ERROROUT("fopen %s faild! use default setting\n", SETTING_BASE);
	}
	else{
		DEBUG("fopen %s success\n", SETTING_BASE);
		memset(tmp_buf, 0, sizeof(tmp_buf));
		
		while(NULL!=fgets(tmp_buf, sizeof(tmp_buf), fp)){
			p_value = setting_item_value(tmp_buf, strlen(tmp_buf), ':');
			if(NULL!=p_value)
			{
				//DEBUG("setting item: %s, value: %s\n", tmp_buf, p_value);
				if(strlen(tmp_buf)>0 && strlen(p_value)>0){
					if(0==strcmp(tmp_buf, "server_id"))
						strncpy(s_service_id, p_value, sizeof(s_service_id)-1);
					else if(0==strcmp(tmp_buf, "root_channel"))
						s_root_channel = atoi(p_value);
					else if(0==strcmp(tmp_buf, "root_push_file"))
						strncpy(s_root_push_file, p_value, sizeof(s_root_push_file)-1);
					else if(0==strcmp(tmp_buf, "root_push_file_size"))
						s_root_push_file_size = atoi(p_value);
					else if(0==strcmp(tmp_buf, "prog_data_pid"))
						s_prog_data_pid = atoi(p_value);
					else if(0==strcmp(tmp_buf, "dbstar_database"))
						strncpy(s_dbstar_database_uri, p_value, sizeof(s_dbstar_database_uri)-1);
					else if(0==strcmp(tmp_buf, "smarthome_database"))
						strncpy(s_smarthome_database_uri, p_value, sizeof(s_smarthome_database_uri)-1);
					else if(0==strcmp(tmp_buf, "dbstar_debug_level"))
						s_debug_level = atoi(p_value);
					else if(0==strcmp(tmp_buf, "parse_xml"))	/* this xml only for parse testing */
						strncpy(s_xml, p_value, sizeof(s_xml)-1);
					else if(0==strcmp(tmp_buf, "initialize_xml"))
						strncpy(s_initialize_xml, p_value, sizeof(s_initialize_xml)-1);
					else if(0==strcmp(tmp_buf, "localcolumn_res"))
						strncpy(s_column_res, p_value, sizeof(s_column_res)-1);
					else if(0==strcmp(tmp_buf, "software_check"))
						s_software_check = atoi(p_value);
				}
			}
			memset(tmp_buf, 0, sizeof(tmp_buf));
		}
		fclose(fp);
	}
	DEBUG("init settings OK\n");
	
	s_settingInitFlag = 1;
	return 0;
}

int setting_uninit()
{
	s_settingInitFlag = 0;
	return 0;
}

int root_channel_get(void)
{
	return s_root_channel;
}

int root_push_file_get(char *filename, unsigned int len)
{
	if(NULL==filename || 0==len)
		return -1;

	strncpy(filename, s_root_push_file, len);
	
	return 0;
}

int root_push_file_size_get(void)
{
	return s_root_push_file_size;
}

int prog_data_pid_get(void)
{
	return s_prog_data_pid;
}

char *dbstar_database_uri()
{
	return s_dbstar_database_uri;
}

char *smartlife_database_uri_get()
{
	return s_smarthome_database_uri;
}

int debug_level_get(void)
{
	return s_debug_level;
}

int software_check(void)
{
	return s_software_check;
}

int parse_xml_get(char *xml_uri, unsigned int size)
{
	if(NULL==xml_uri || 0==size)
		return -1;
	
	strncpy(xml_uri, s_xml, size);
	return 0;
}

int initialize_xml_get()
{
	return atoi(s_initialize_xml);
}

char *column_res_get()
{
	return s_column_res;
}

int factory_renew(void)
{
	DEBUG("CAUTION: begin to renew factory status\n");

	unlink(DBSTAR_DATABASE);
	unlink(SETTING_BASE);
	sync();
	sleep(1);
	
	return 0;
}

int reboot(void)
{
	DEBUG("need reboot the stb, this is a phony action\n");
	return 0;
}

int poweroff(void)
{
	DEBUG("need power off the stb, this is a phony action\n");
	return 0;
}

int alarm_ring(void)
{
	return -1;
}


#ifdef SOLARIS
#include <sys/sockio.h>
#endif

// only for IP v4
#define MAXINTERFACES   16

/*
功能：获取指定网卡的ip、状态、mac地址
输入：interface_name ——网卡名称，如："eth0"、"lo"
输出：	ip		——点分十进制的IP v4地址，如："192.168.100.100"
		status	——状态，如："UP"、"DOWN"
		mac		——MAC地址，以分号隔开的16进制表示，如："00:0c:29:50:fc:f8"
返回：0——成功；其他——失败
*/
int ifconfig_get(char *interface_name, char *ip, char *status, char *mac)
{
	if(NULL==interface_name || (NULL==ip && NULL==status && NULL==mac)){
		DEBUG("some params are invalid\n");
		return -1;
	}
	
#ifdef SOLARIS
	struct arpreq arp;
#endif
	register int fd=0, interface_num=0, ret = 0;
	struct ifreq buf[MAXINTERFACES];
	struct ifconf ifc;
	
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0)
	{
		DEBUG("create socket(%d) to read ifconfig infor\n", fd);
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = (caddr_t) buf;
		if (!ioctl (fd, SIOCGIFCONF, (char *) &ifc))
		{
			//获取接口信息
			interface_num = ifc.ifc_len / sizeof (struct ifreq);
			DEBUG("interface num is %d\n",interface_num);
			//根据接口信息循环获取设备IP和MAC地址
			while (interface_num-- > 0)
			{
				//获取设备名称
				DEBUG ("net device %d: %s\n", interface_num, buf[interface_num].ifr_name);
				if(strcmp(interface_name, buf[interface_num].ifr_name)){
					continue;
				}
				
				ret = 0;
				//判断网卡类型
				if (!(ioctl (fd, SIOCGIFFLAGS, (char *) &buf[interface_num])))
				{
					if (buf[interface_num].ifr_flags & IFF_PROMISC)
					{
						DEBUG("the interface_num is PROMISC\n");
					}
				}
				else
				{
					char str[256];
					sprintf (str, "cpm: ioctl device %s", buf[interface_num].ifr_name);
					perror (str);
					ret = -1;
					break;
				}
				
				//判断网卡状态
				if(NULL!=status)
				{
					if (buf[interface_num].ifr_flags & IFF_UP)
					{
						DEBUG("the interface_num status is UP\n");
						strcpy(status, "UP");
					}
					else
					{
						DEBUG("the interface_num status is DOWN\n");
						strcpy(status, "DOWN");
					}
				}
				
				//获取当前网卡的IP地址
				if(NULL!=ip)
				{
					if (!(ioctl (fd, SIOCGIFADDR, (char *) &buf[interface_num])))
					{
						strcpy(ip, (char *)inet_ntoa(((struct sockaddr_in*)(&buf[interface_num].ifr_addr))->sin_addr));
						DEBUG ("IP address is: %s\n", ip);
					}
					else
					{
						char str[256];
						sprintf (str, "cpm: ioctl device %s", buf[interface_num].ifr_name);
						perror (str);
						ret = -1;
						break;
					}
				}
				if(NULL!=mac)
				{
		
#ifdef SOLARIS		/* this section can't get Hardware Address,I don't know whether the reason is module driver*/
					//获取MAC地址
					arp.arp_pa.sa_family = AF_INET;
					arp.arp_ha.sa_family = AF_INET;
					((struct sockaddr_in*)&arp.arp_pa)->sin_addr.s_addr=((struct sockaddr_in*)(&buf[interface_num].ifr_addr))->sin_addr.s_addr;
					if (!(ioctl (fd, SIOCGARP, (char *) &arp)))
					{
						puts ("HW address is:");     //以十六进制显示MAC地址
						printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
							(unsigned char)arp.arp_ha.sa_data[0],
							(unsigned char)arp.arp_ha.sa_data[1],
							(unsigned char)arp.arp_ha.sa_data[2],
							(unsigned char)arp.arp_ha.sa_data[3],
							(unsigned char)arp.arp_ha.sa_data[4],
							(unsigned char)arp.arp_ha.sa_data[5]);
						puts("");
						puts("");
					}
#else
	#if 0
					/*Get HW ADDRESS of the net card */
					if (!(ioctl (fd, SIOCGENADDR, (char *) &buf[interface_num])))
					{
						puts ("HW address is:");
						printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
							(unsigned char)buf[interface_num].ifr_enaddr[0],
							(unsigned char)buf[interface_num].ifr_enaddr[1],
							(unsigned char)buf[interface_num].ifr_enaddr[2],
							(unsigned char)buf[interface_num].ifr_enaddr[3],
							(unsigned char)buf[interface_num].ifr_enaddr[4],
							(unsigned char)buf[interface_num].ifr_enaddr[5]);
						puts("");
						puts("");
					}
	#endif

					if (!(ioctl (fd, SIOCGIFHWADDR, (char *) &buf[interface_num])))
					{
						sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
							(unsigned char)buf[interface_num].ifr_hwaddr.sa_data[0],
							(unsigned char)buf[interface_num].ifr_hwaddr.sa_data[1],
							(unsigned char)buf[interface_num].ifr_hwaddr.sa_data[2],
							(unsigned char)buf[interface_num].ifr_hwaddr.sa_data[3],
							(unsigned char)buf[interface_num].ifr_hwaddr.sa_data[4],
							(unsigned char)buf[interface_num].ifr_hwaddr.sa_data[5]);
						DEBUG("hardware address: %s\n", mac);
					}
#endif
					else
					{
						char str[256];
						sprintf (str, "cpm: ioctl device %s", buf[interface_num].ifr_name);
						perror (str);
						ret = -1;
						break;
					}
				}
				break;					// 只完成一个有效循环即可。
			} //while
		}
		else{
			ERROROUT ("cpm: ioctl");
			ret = -1;
		}
		
		close (fd);
		DEBUG("close fd(%d)\n", fd);
	}
	else{
		ERROROUT ("cpm: socket");
		ret = -1;
	}
	
	return ret;
}

void upgrade_sign_set()
{
	unsigned char mark = 0;
	LoaderInfo_t out;
	
	memset(&out, 0, sizeof(out));
	if(0==get_loader_message(&mark, &out))
	{
		DEBUG("read loader msg: %d, set to 3\n", mark);
		set_loader_reboot_mark(3);
	}
	else
		DEBUG("get loader message failed\n");
}

/*
 检查指定的成品id是用户选择接收还是选择不接收
 return 1——用户选择接收（默认）；return 0——用户选择不接收；return -1——检查失败
*/
int guidelist_select_status(const char *publication_id)
{
	// 制表符\t
	char *p_HT = NULL;
	char *p_list = s_guidelist_unselect;
	int ret = 1;
	
	while(NULL!=p_list){
		p_HT = strchr(p_list,'\t');
		if(p_HT){
			*p_HT = '\0';
			p_HT ++;
		}
		
		if(0==strcmp(publication_id,p_list)){
			ret = 0;
			break;
		}
		
		p_list = p_HT;
	}
	
	return ret;
}

static int guidelist_select_refresh_cb(char **result, int row, int column, void *receiver, unsigned int receiver_size)
{
	DEBUG("sqlite callback, row=%d, column=%d, receiver addr=%p, receive_size=%u\n", row, column, receiver,receiver_size);
	if(row<1){
		DEBUG("no record in table, return\n");
		return 0;
	}
	
	int i = 0;
	int list_size = (64+1)*row;
	char *p_list = (char *)malloc(list_size);
	if(NULL==p_list){
		DEBUG("malloc buffer %d failed\n", list_size);
		return -1;
	}
	else
		DEBUG("malloc buffer(%p) %d OK\n", p_list, list_size);
		
	memset(p_list,0,sizeof(p_list));
	
	for(i=1;i<row+1;i++)
	{
		if(i>1)
			snprintf(p_list+strlen(p_list),list_size-strlen(p_list),"\t");
		snprintf(p_list+strlen(p_list),list_size-strlen(p_list),"%s",result[i*column]);
	}
	*((char **)receiver) = p_list;
	
	return 0;
}

int guidelist_select_refresh()
{
	if(s_guidelist_unselect){
		free(s_guidelist_unselect);
		DEBUG("free %p\n", s_guidelist_unselect);
	}
	
	char sqlite_cmd[256];
	int (*sqlite_callback)(char **, int, int, void *, unsigned int) = guidelist_select_refresh_cb;
	
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT PublicationID FROM GuideList WHERE UserStatus='0';");
	int ret = sqlite_read(sqlite_cmd, (void *)(&s_guidelist_unselect), sizeof(s_guidelist_unselect), sqlite_callback);
	if(ret>0){
		DEBUG("unselect by user[%p]: %s\n",s_guidelist_unselect,s_guidelist_unselect);
		return 0;
	}
	else if(0==ret){
		DEBUG("no guidelist unselect record\n");
		return 0;
	}
	else{	// (ret<0)
		DEBUG("guidelist_unselect_refresh failed\n");
		return -1;
	}
}


#define REMOTE_FUTURE		"9999-99-99 99:99:99"
#define TIME_STAMP_MIN		"2013-02-01 00:00:00"
#define DELETE_SIZE_ONCE	(107374182400LL)
//(107374182400)==(100*1024*1024*1024)==100G
static long long s_delete_total_size = 0LL;

static int disk_manage_cb(char **result, int row, int column, void *receiver, unsigned int receiver_size)
{
	DEBUG("sqlite callback, row=%d, column=%d, receiver addr=%p, receive_size=%u\n", row, column, receiver,receiver_size);
	if(row<1){
		DEBUG("no record in table, return\n");
		return 0;
	}
	
	int i = 0;
	long long total_size = 0LL;
	char total_uri[512];
	char *ids = (char *)receiver;
	int ret = 0;
	
	// PublicationID,URI,TotalSize,PushEndTime,TimeStamp,Deleted,ReceiveStatus
	for(i=1;i<row+1;i++)
	{
		// 如果此节目还没有下载完毕，先反注册
		if(0==atoi(result[i*column+6])){
			ret = push_dir_unregister(result[i*column+1]);
			PRINTF("push_dir_unregister(%s) = %d\n", result[i*column+1], ret);
		}
		
		sscanf(result[i*column+2],"%lld", &total_size);
		DEBUG("%s\t#%s\t#%s=%lld\t#%s\t#%s\t#%s\n",result[i*column],result[i*column+1],result[i*column+2],total_size,result[i*column+3],result[i*column+4],result[i*column+5]);
		
		snprintf(total_uri,sizeof(total_uri),"%s/%s",push_dir_get(),result[i*column+1]);
		remove_force(total_uri);
		
		if(strlen(ids)>0)
			snprintf(ids+strlen(ids),receiver_size-strlen(ids),"\t");
		snprintf(ids+strlen(ids),receiver_size-strlen(ids),"%s",result[i*column]);
		
		s_delete_total_size += total_size;
		if(s_delete_total_size>=DELETE_SIZE_ONCE){
			DEBUG("delete %lld finished, %s, total finish!\n", s_delete_total_size,total_uri);
			break;
		}
		else
			DEBUG("delete %lld finished, %s\n", s_delete_total_size, total_uri);
		
		if(strlen(ids)>(receiver_size-64)){
			DEBUG("receiver can load no more than such PublicationID\n");
			break;
		}
		else
			DEBUG("publication in delete queue: %s", ids);
	}
	
	// 还需要找个机会删除这些Publication对应的ResStr、ResPoster等附属记录
	
	return 0;
}

/*
 目前只做一个简单的磁盘整理，不考虑未纳入数据库管理的文件（需要扫描磁盘才能实现）
 1、参数不是必须的，如果两个参数都为空，则按照FIFO原则从数据库中挨个删除；
 2、如果PublicationID不为空，则按照指定的ID进行删除；
 3、如果PublicationID为空，但ProductID不为空，则按照指定的产品进行删除。
*/
int disk_manage(char *PublicationID, char *ProductID)
{
	char sqlite_cmd[2048];
	int (*sqlite_callback)(char **, int, int, void *, unsigned int) = disk_manage_cb;
	char deleted_publicationids[1024];
	memset(deleted_publicationids,0,sizeof(deleted_publicationids));

#if 0
不考虑时间过滤
	char time_stamp[32];
	memset(time_stamp,0,sizeof(time_stamp));
	localtime_rf(time_stamp, sizeof(time_stamp));
	
	if(strcmp(time_stamp,TIME_STAMP_MIN)<0){
		DEBUG("such time stamp for now is invalid: %s, replace it as %s\n", time_stamp,REMOTE_FUTURE);
		snprintf(time_stamp,sizeof(time_stamp),"%s",REMOTE_FUTURE);
	}

	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT PublicationID,URI,TotalSize,PushEndTime,TimeStamp,Deleted FROM Publication WHERE (PushEndTime<'%s' AND ReceiveStatus!='0') GROUP BY PublicationID ORDER BY IsReserved,Deleted DESC,ReceiveStatus,TimeStamp LIMIT 16;",time_stamp);
#else
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT PublicationID,URI,TotalSize,PushEndTime,TimeStamp,Deleted,ReceiveStatus FROM Publication");
	if(NULL!=PublicationID && strlen(PublicationID)>0){
		snprintf(sqlite_cmd+strlen(sqlite_cmd),sizeof(sqlite_cmd)-strlen(sqlite_cmd)," WHERE PublicationID='%s';",PublicationID);
	}
	else if(NULL!=ProductID && strlen(ProductID)>0){
		snprintf(sqlite_cmd+strlen(sqlite_cmd),sizeof(sqlite_cmd)-strlen(sqlite_cmd)," WHERE URI='%s' GROUP BY PublicationID;",ProductID);
	}
	else{
		snprintf(sqlite_cmd+strlen(sqlite_cmd),sizeof(sqlite_cmd)-strlen(sqlite_cmd)," WHERE ReceiveStatus!='0' GROUP BY PublicationID ORDER BY IsReserved,Deleted DESC,ReceiveStatus,TimeStamp LIMIT 16;");
	}
	DEBUG("%s\n", sqlite_cmd);
#endif

	s_delete_total_size = 0LL;
	int ret = sqlite_read(sqlite_cmd, deleted_publicationids, sizeof(deleted_publicationids), sqlite_callback);
	if(ret>0){
		DEBUG("delete such Publications: %s\n", deleted_publicationids);
		char *p_publicationid = deleted_publicationids;
		
		// 制表符\t
		char *p_HT = NULL;
		int first_publicaiton_flag = 1;
		
		snprintf(sqlite_cmd, sizeof(sqlite_cmd), "DELETE FROM Publication WHERE ");
		
		char sqlite_cmd_ResStr[4096];
		snprintf(sqlite_cmd_ResStr, sizeof(sqlite_cmd_ResStr), "DELETE FROM ResStr WHERE ");
		
		char sqlite_cmd_ResPoster[2048];
		snprintf(sqlite_cmd_ResPoster, sizeof(sqlite_cmd_ResPoster), "DELETE FROM ResPoster WHERE ");
		
		char sqlite_cmd_ResSubTitle[2048];
		snprintf(sqlite_cmd_ResSubTitle, sizeof(sqlite_cmd_ResSubTitle), "DELETE FROM ResSubTitle WHERE ");
		
		char sqlite_cmd_MultipleLanguageInfoVA[2048];
		snprintf(sqlite_cmd_MultipleLanguageInfoVA, sizeof(sqlite_cmd_MultipleLanguageInfoVA), "DELETE FROM MultipleLanguageInfoVA WHERE ");
		
		char sqlite_cmd_Initialize[2048];
		snprintf(sqlite_cmd_Initialize, sizeof(sqlite_cmd_Initialize), "DELETE FROM Initialize WHERE ");

		char sqlite_cmd_Preview[2048];
		snprintf(sqlite_cmd_Preview, sizeof(sqlite_cmd_Preview), "DELETE FROM Preview WHERE ");
		
		while(NULL!=p_publicationid){
			p_HT = strchr(p_publicationid,'\t');
			if(p_HT){
				*p_HT = '\0';
				p_HT ++;
			}
			DEBUG("p_publicationid: %s, p_HT: %s\n", p_publicationid, p_HT);
			
			if(strlen(p_publicationid)>0){
				if(0==first_publicaiton_flag){
					snprintf(sqlite_cmd+strlen(sqlite_cmd),sizeof(sqlite_cmd)-strlen(sqlite_cmd)," OR");
					snprintf(sqlite_cmd_ResStr+strlen(sqlite_cmd_ResStr),sizeof(sqlite_cmd_ResStr)-strlen(sqlite_cmd_ResStr)," OR");
					snprintf(sqlite_cmd_ResPoster+strlen(sqlite_cmd_ResPoster),sizeof(sqlite_cmd_ResPoster)-strlen(sqlite_cmd_ResPoster)," OR");
					snprintf(sqlite_cmd_ResSubTitle+strlen(sqlite_cmd_ResSubTitle),sizeof(sqlite_cmd_ResSubTitle)-strlen(sqlite_cmd_ResSubTitle)," OR");
					snprintf(sqlite_cmd_MultipleLanguageInfoVA+strlen(sqlite_cmd_MultipleLanguageInfoVA),sizeof(sqlite_cmd_MultipleLanguageInfoVA)-strlen(sqlite_cmd_MultipleLanguageInfoVA)," OR");
					snprintf(sqlite_cmd_Initialize+strlen(sqlite_cmd_Initialize),sizeof(sqlite_cmd_Initialize)-strlen(sqlite_cmd_Initialize)," OR");
					snprintf(sqlite_cmd_Preview+strlen(sqlite_cmd_Preview),sizeof(sqlite_cmd_Preview)-strlen(sqlite_cmd_Preview)," OR");
				}
				
				snprintf(sqlite_cmd+strlen(sqlite_cmd),sizeof(sqlite_cmd)-strlen(sqlite_cmd)," PublicationID='%s'", p_publicationid);
				snprintf(sqlite_cmd_ResStr+strlen(sqlite_cmd_ResStr),sizeof(sqlite_cmd_ResStr)-strlen(sqlite_cmd_ResStr)," ((ObjectName='Publication' OR ObjectName='MFile') AND EntityID='%s')", p_publicationid);
				snprintf(sqlite_cmd_ResPoster+strlen(sqlite_cmd_ResPoster),sizeof(sqlite_cmd_ResPoster)-strlen(sqlite_cmd_ResPoster)," (ObjectName='Publication' AND EntityID='%s')", p_publicationid);
				snprintf(sqlite_cmd_ResSubTitle+strlen(sqlite_cmd_ResSubTitle),sizeof(sqlite_cmd_ResSubTitle)-strlen(sqlite_cmd_ResSubTitle)," (ObjectName='Publication' AND EntityID='%s')", p_publicationid);
				snprintf(sqlite_cmd_MultipleLanguageInfoVA+strlen(sqlite_cmd_MultipleLanguageInfoVA),sizeof(sqlite_cmd_MultipleLanguageInfoVA)-strlen(sqlite_cmd_MultipleLanguageInfoVA)," PublicationID='%s'", p_publicationid);
				snprintf(sqlite_cmd_Initialize+strlen(sqlite_cmd_Initialize),sizeof(sqlite_cmd_Initialize)-strlen(sqlite_cmd_Initialize)," (PushFlag='%d' AND ID='%s')", PUBLICATION_XML,p_publicationid);
				snprintf(sqlite_cmd_Preview+strlen(sqlite_cmd_Preview),sizeof(sqlite_cmd_Preview)-strlen(sqlite_cmd_Preview)," (PublicationID='%s')", p_publicationid);
				
				if(1==first_publicaiton_flag)
					first_publicaiton_flag = 0;
			}
				
			p_publicationid = p_HT;
		}
		
		if(0==first_publicaiton_flag){
			snprintf(sqlite_cmd+strlen(sqlite_cmd),sizeof(sqlite_cmd)-strlen(sqlite_cmd),";");
			snprintf(sqlite_cmd_ResStr+strlen(sqlite_cmd_ResStr),sizeof(sqlite_cmd_ResStr)-strlen(sqlite_cmd_ResStr),";");
			snprintf(sqlite_cmd_ResPoster+strlen(sqlite_cmd_ResPoster),sizeof(sqlite_cmd_ResPoster)-strlen(sqlite_cmd_ResPoster),";");
			snprintf(sqlite_cmd_ResSubTitle+strlen(sqlite_cmd_ResSubTitle),sizeof(sqlite_cmd_ResSubTitle)-strlen(sqlite_cmd_ResSubTitle),";");
			snprintf(sqlite_cmd_MultipleLanguageInfoVA+strlen(sqlite_cmd_MultipleLanguageInfoVA),sizeof(sqlite_cmd_MultipleLanguageInfoVA)-strlen(sqlite_cmd_MultipleLanguageInfoVA),";");
			snprintf(sqlite_cmd_Initialize+strlen(sqlite_cmd_Initialize),sizeof(sqlite_cmd_Initialize)-strlen(sqlite_cmd_Initialize),";");
			snprintf(sqlite_cmd_Preview+strlen(sqlite_cmd_Preview),sizeof(sqlite_cmd_Preview)-strlen(sqlite_cmd_Preview),";");
			
			if(-1==sqlite_transaction_begin()){
				ret = -1;
			}
			else{
				sqlite_transaction_exec(sqlite_cmd);
				sqlite_transaction_exec(sqlite_cmd_ResStr);
				sqlite_transaction_exec(sqlite_cmd_ResPoster);
				sqlite_transaction_exec(sqlite_cmd_ResSubTitle);
				sqlite_transaction_exec(sqlite_cmd_MultipleLanguageInfoVA);
				sqlite_transaction_exec(sqlite_cmd_Initialize);
				sqlite_transaction_exec(sqlite_cmd_Preview);
				
				sqlite_transaction_end(1);
			}
			DEBUG("disk manage finished\n");
		}
		else
			DEBUG("no publication has deleted\n");

		return 0;
	}
	else if(0==ret){
		DEBUG("select no progs for disk manage OK\n");
		return 0;
	}
	else{	// (ret<0)
		DEBUG("select progs for disk manage failed\n");
		return -1;
	}	
}

static void drm_errors(char *fun, CDCA_U16 ret)
{
	switch(ret){
		case CDCA_RC_CARD_INVALID:
			DEBUG("[%s] none or invalid smart card\n", fun);
			break;
		case CDCA_RC_POINTER_INVALID:
			DEBUG("[%s] null pointor\n", fun);
			break;
		case CDCA_RC_DATA_NOT_FIND:
			DEBUG("[%s] no such operator\n", fun);
			break;
		default:
			DEBUG("[%s] such return can not distinguished: %d\n", fun, ret);
			break;
	}
}

static int smartcard_sn_get(char *buf, unsigned int size)
{
	if(NULL==buf || 0==size){
		DEBUG("invalid args\n");
		return -1;
	}
	
	int ret = -1;
	
	ret = CDCASTB_GetCardSN(buf);
	if(CDCA_RC_OK==ret){
		DEBUG("read smartcard sn OK: %s\n", buf);
		ret = 0;
	}
	else{
		drm_errors("CDCASTB_GetCardSN", ret);
		snprintf(buf,size,"SMARTCARD_USELESS");
		ret = -1;
	}
	
	return ret;
}

static int drmlib_version_get(char *buf, unsigned int size)
{
	if(NULL==buf || 0==size){
		DEBUG("invalid args\n");
		return -1;
	}
/*
 查询CA_LIB版本号，要求机顶盒以16进制显示
*/
	snprintf(buf,size,"3.0(0x%lx)", CDCASTB_GetVer());
	DEBUG("CA_LIB Ver: %s\n", buf);
	
	return 0;
}

#if 1
static int smartcard_eigenuvalue_get(char *buf, unsigned int size)
{
	if(NULL==buf || 0==size){
		DEBUG("invalid args\n");
		return -1;
	}
/*
 查询智能卡特征码
*/
	CDCA_U16	wArrTvsID[CDCA_MAXNUM_OPERATOR];
	CDCA_U32	ACArray[CDCA_MAXNUM_ACLIST];	
	CDCA_U8		index = 0;
	CDCA_U16	j = 0;
	
	memset(wArrTvsID, 0, sizeof(wArrTvsID));
	CDCA_U16 ret = CDCASTB_GetOperatorIds(wArrTvsID);
	if(CDCA_RC_OK==ret){
		for(index=0;index<CDCA_MAXNUM_OPERATOR;index++){
			if(0==wArrTvsID[index]){
				DEBUG("OperatorID list end\n");
				break;
			}
			else{
				DEBUG("OperatorID: %d\n", wArrTvsID[index]);
				memset(ACArray, 0, sizeof(ACArray));
				ret = CDCASTB_GetACList(wArrTvsID[j], ACArray);
				if(CDCA_RC_OK==ret){
					int max_ac_num = CDCA_MAXNUM_ACLIST>6?6:CDCA_MAXNUM_ACLIST;
					for(index=0;index<max_ac_num;index++){
						DEBUG("ACArray[%d]=(%lu)\n",index, ACArray[index]);
						if(0!=ACArray[index]){
							DEBUG("Operator: %d, ACArray[%d]:%lu\n", wArrTvsID[j],index,ACArray[index]);
							if(0==index)
								snprintf(buf, size, "ID%d: %lu",index,ACArray[index]);
							else
								snprintf(buf+strlen(buf), size-strlen(buf), "\nID%d: %lu",index,ACArray[index]);
						}
					}
				}
				else
					drm_errors("CDCASTB_GetACList", ret);
			}
		}
	}
	else{
		drm_errors("CDCASTB_GetOperatorIds", ret);
		return -1;
	}
	
	DEBUG("%s\n", buf);
	
	return 0;
}


static int smartcard_entitleinfo_get(char *buf, unsigned int size)
{
	if(NULL==buf || 0==size){
		DEBUG("invalid args\n");
		return -1;
	}
	
/*
 查询授权信息
*/
	CDCA_U32 dwFrom = 0, dwNum = 128;
	unsigned int i = 0;
	SCDCAPVODEntitleInfo EntitleInfo[128];
	char		BeginDate[64];
	char		ExpireDate[64];

	int ret = CDCASTB_DRM_GetEntitleInfo(&dwFrom,EntitleInfo,&dwNum);
	if(CDCA_RC_OK==ret){
		DEBUG("dwFrom=%lu, dwNum=%lu\n", dwFrom, dwNum);
		for(i=0;i<dwNum;i++){
			if(0!=EntitleInfo[i].m_ID){
				memset(BeginDate, 0, sizeof(BeginDate));
				memset(ExpireDate, 0, sizeof(ExpireDate));
				if(		0==drm_time_convert(EntitleInfo[i].m_ProductStartTime, BeginDate, sizeof(BeginDate))
					&& 	0==drm_time_convert(EntitleInfo[i].m_ProductEndTime, ExpireDate, sizeof(ExpireDate))){
					;
				}
				
				if(0==i)
					snprintf(buf,size,"%d\t%lu\t%s\t%s\t%lu",EntitleInfo[i].m_OperatorID,EntitleInfo[i].m_ID,BeginDate,ExpireDate,EntitleInfo[i].m_LimitTotaltValue);
				else
					snprintf(buf+strlen(buf),size-strlen(buf),"\n%d\t%lu\t%s\t%s\t%lu",EntitleInfo[i].m_OperatorID,EntitleInfo[i].m_ID,BeginDate,ExpireDate,EntitleInfo[i].m_LimitTotaltValue);
			}
		}
		DEBUG("%s\n", buf);
		
		return 0;
	}
	else{
		drm_errors("CDCASTB_DRM_GetEntitleInfo", ret);
		return -1;
	}
	
}

#define ENTITLE_SIZE_MIN	(3690LL)
#define ENTITLE_STORE "/mnt/sdb1"
//#define ENTITLE_STORE "/mnt/sdcard/external_sdcard"
static int smartcard_EntitleFile_output(char *retbuf, unsigned int retbuf_size)
{
	char CardSN[CDCA_MAXLEN_SN+1];
	char external_entitle_file[512];
	int ret = -1;
	
	struct statfs diskInfo;
    if(0==statfs(ENTITLE_STORE,&diskInfo)){
	    unsigned long long totalBlocks = diskInfo.f_bsize;
	    unsigned long long totalSize = totalBlocks * diskInfo.f_blocks;
	    DEBUG("TOTAL_SIZE == %llu B\n",totalSize);
	
	    unsigned long long freeDisk = diskInfo.f_bfree*totalBlocks;
	    DEBUG("DISK_FREE  == %llu B\n",freeDisk);
	    
	    if(freeDisk>ENTITLE_SIZE_MIN){
	    	memset(CardSN,0,sizeof(CardSN));
			ret = CDCASTB_GetCardSN(CardSN);
			if(CDCA_RC_OK==ret){
				if(0==smartcard_entitleinfo_get(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space)) && strlen(s_jni_cmd_public_space)>4){
					snprintf(external_entitle_file,sizeof(external_entitle_file),"%s/%s", ENTITLE_STORE,CardSN);
					DEBUG("smartcard %s, output to %s\n", CardSN, external_entitle_file);
					int fd = open(external_entitle_file,O_WRONLY|O_CREAT,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
					if(-1!=fd){
						ret = CDCASTB_DRM_ExportEntitleFile(CardSN,(void *)&fd);
						if(CDCA_RC_OK==ret){
							DEBUG("output entitle file OK\n");
							snprintf(retbuf,retbuf_size,"ENTITLE_OUTPUT_FINISH");
							ret = 0;
						}
						else if(CDCA_RC_POINTER_INVALID==ret){
							DEBUG("0x%x CDCA_RC_POINTER_INVALID\n",CDCA_RC_POINTER_INVALID);
							snprintf(retbuf,retbuf_size,"NO_DEVICE");
							ret = -1;
						}
						else if(CDCA_RC_NOENTITLEDATA==ret){
							DEBUG("0x%x CDCA_RC_NOENTITLEDATA\n",CDCA_RC_NOENTITLEDATA);
							snprintf(retbuf,retbuf_size,"NO_ENTITLE");
							ret = -1;
						}
						else if(CDCA_RC_SYSTEMERR==ret){
							DEBUG("0x%x CDCA_RC_SYSTEMERR\n",CDCA_RC_SYSTEMERR);
							snprintf(retbuf,retbuf_size,"NO_DEVICE");
							ret = -1;
						}
						else{
							DEBUG("CDCASTB_DRM_ExportEntitleFile faild, %d=0x%x\n", ret,ret);
							ret = -1;
						}
							
						close(fd);
						system("sync");
					}
					else{
						ERROROUT("open %s to save entitle failed\n", external_entitle_file);
						snprintf(retbuf,retbuf_size,"NO_DEVICE");
						ret = -1;
					}
				}
				else{
					DEBUG("no entitle info to output\n");
					snprintf(retbuf,retbuf_size,"NO_ENTITLE");
					ret = -1;
				}
			}
			else{
				drm_errors("CDCASTB_GetCardSN", ret);
				snprintf(retbuf,retbuf_size,"NO_ENTITLE");
				ret = -1;
			}
	    }
	    else{
	    	DEBUG("no enough space for smartmard entitle output\n");
	    	snprintf(retbuf,retbuf_size,"NOT_ENOUGH_SPACE");
	    	ret = -1;
	    }
	}
	else{
		ERROROUT("statfs(%s)\n", ENTITLE_STORE);
		snprintf(retbuf,retbuf_size,"NO_DEVICE");
	}
	
	return ret;
}

static int smartcard_EntitleFile_input(char *retbuf, unsigned int retbuf_size)
{
	char CardSN[CDCA_MAXLEN_SN+1];
	char external_entitle_file[512];
	
	memset(CardSN,0,sizeof(CardSN));
	int ret = CDCASTB_GetCardSN(CardSN);
	if(CDCA_RC_OK==ret){
		snprintf(external_entitle_file,sizeof(external_entitle_file),"%s/%s", ENTITLE_STORE,CardSN);
		DEBUG("smartcard %s, input from %s\n", CardSN, external_entitle_file);
		int fd = open(external_entitle_file,O_RDONLY);
		if(-1!=fd){
			ret = CDCASTB_DRM_ImportEntitleFile((void *)&fd);
			if(CDCA_RC_OK==ret){
				DEBUG("input entitle file OK\n");
				snprintf(retbuf,retbuf_size,"ENTITLE_INPUT_FINISH");
				ret = 0;
			}
			else{
				DEBUG("input entitle file failed\n");
				snprintf(retbuf,retbuf_size,"ENTITLE_INPUT_INTERRUPT");
				ret = -1;
			}
			
			close(fd);
		}
		else{
			ERROROUT("open %s to read entitle failed\n", external_entitle_file);
			snprintf(retbuf,retbuf_size,"ENTITLE_INPUT_INTERRUPT");
			ret = -1;
		}
	}
	else{
		drm_errors("CDCASTB_GetCardSN", ret);
		snprintf(retbuf,retbuf_size,"ENTITLE_INPUT_INTERRUPT");
		ret = -1;
	}
	
	return ret;
}

static int DRM_emailheads_get(char *buf, unsigned int size)
{
	if(NULL==buf || 0==size){
		DEBUG("invalid args\n");
		return -1;
	}
	
	SCDCAEmailHead EmailHeads[8];
	CDCA_U8 byCount = 0;
	CDCA_U8 byFromIndex = 0;
	int i = 0;
	char email_createtime[64];
	int ret = -1;
	
	while(1){
		ret = CDCASTB_GetEmailHeads(EmailHeads,&byCount,&byFromIndex);
		if(CDCA_RC_OK==ret){
/*
应当根据邮件的日期顺序排序
*/
			for(i=0;i<byCount;i++)
			{
				memset(email_createtime,0,sizeof(email_createtime));
				drm_time_convert(EmailHeads[i].m_tCreateTime, email_createtime, sizeof(email_createtime));
				if(0==i)
					snprintf(buf,size,"%lu\t%s\t%d\t%s",EmailHeads[i].m_dwActionID,email_createtime,EmailHeads[i].m_bNewEmail,EmailHeads[i].m_szEmailHead);
				else
					snprintf(buf+strlen(buf),size-strlen(buf),"\n%lu\t%s\t%d\t%s",EmailHeads[i].m_dwActionID,email_createtime,EmailHeads[i].m_bNewEmail,EmailHeads[i].m_szEmailHead);
			}
			
			if(byCount<10){
				DEBUG("get email head finish\n");
				break;
			}
		}
		else{
			drm_errors("CDCASTB_GetEmailHeads", ret);
			break;
		}
	}
	
	return ret;
}

static int DRM_emailcontent_get(char *emailID, char *buf, unsigned int size)
{
	if(NULL==emailID || NULL==buf || 0==size){
		DEBUG("invalid args\n");
		return -1;
	}

	SCDCAEmailContent EmailContent;
	memset(&EmailContent,0,sizeof(EmailContent));
	
	int ret = CDCASTB_GetEmailContent(strtol(emailID,NULL,0),&EmailContent);
	if(CDCA_RC_OK==ret){
		snprintf(buf,size,"%s", EmailContent.m_szEmail);
		return 0;
	}
	else{
		drm_errors("CDCASTB_GetEmailContent", ret);
		return -1;
	}
}

static int DRM_programinfo_get(char *PublicationID, char *buf, unsigned int size)
{
	if(NULL==PublicationID || NULL==buf || 0==size){
		DEBUG("invalid args\n");
		return -1;
	}

	SCDCAPVODProgramInfo ProgramInfo[8];
	CDCA_U32 dwFrom = 0;
	CDCA_U32 dwNum = 8;
	char		BeginDate[64];
	char		ExpireDate[64];
	CDCA_U32 i = 0;
	
	char sqlite_cmd[256];
	char DRMFile[256];
	memset(DRMFile, 0, sizeof(DRMFile));
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT DRMFile from Publication where PublicationID='%s';",PublicationID);
	if(-1==str_sqlite_read(DRMFile,sizeof(DRMFile),sqlite_cmd)){
		DEBUG("can not read DRMFile for PublicationID: %s\n", PublicationID);
		return -1;
	}
	else{
		DEBUG("should op DRMFile: %s\n", DRMFile);
		int fd = open(DRMFile,O_RDONLY);
		if(-1!=fd){
			int ret = CDCASTB_DRM_GetProgramInfo((void *)&fd,&dwFrom,ProgramInfo,&dwNum);
			if(CDCA_RC_OK==ret){
				for(i=0;i<dwNum;i++){
					if(0==i)
						snprintf(buf,size,"%d",ProgramInfo[i].m_OperatorID);
					else
						snprintf(buf+strlen(buf),size-strlen(buf),"\n%d",ProgramInfo[i].m_OperatorID);
					
					int j = 0;
					
					for(j=0;j<ProgramInfo[i].m_PackNum;j++){
						memset(BeginDate, 0, sizeof(BeginDate));
						memset(ExpireDate, 0, sizeof(ExpireDate));
						if(		0==drm_time_convert(ProgramInfo[i].m_Packs[j].m_IssueStartTime, BeginDate, sizeof(BeginDate))
							&& 	0==drm_time_convert(ProgramInfo[i].m_Packs[j].m_IssueEndTime, ExpireDate, sizeof(ExpireDate))){
							;
						}
						if(0==i)
							snprintf(buf,size,"%d\t%lu\t%s\t%s",ProgramInfo[i].m_OperatorID,ProgramInfo[i].m_Packs[j].m_ID,BeginDate,ExpireDate);
						else
							snprintf(buf+strlen(buf),size-strlen(buf),"\n%d\t%lu\t%s\t%s",ProgramInfo[i].m_OperatorID,ProgramInfo[i].m_Packs[j].m_ID,BeginDate,ExpireDate);
					}
				}
				DEBUG("%s\n", buf);
				return 0;
			}
			else{
				drm_errors("CDCASTB_DRM_GetProgramInfo", ret);
				return -1;
			}
		}
		else{
			ERROROUT("open %s failed\n",DRMFile);
			return -1;
		}
	}
}
#endif

/*
 通过jni提供给UI使用的函数，UI可以由此设置向上发送消息的回调函数。
 实际调用参见dvbpush_jni.c
*/
int dvbpush_register_notify(void *func)
{
	DEBUG("dvbpush_register_notify\n");
	if (func != NULL)
		dvbpush_notify = (dvbpush_notify_t)func;

	return 0;
}

/*
 底层通过此函数发送消息到上层。
 
*/
int msg_send2_UI(int type, char *msg, int len)
{
	DEBUG("type: %d=0x%x, msg: %s, len: %d\n", type,type, msg, len);
	if (dvbpush_notify != NULL){
		return dvbpush_notify(type, msg, len);
	}
	else{
		DEBUG("there is no callback to send msg\n");
		return -1;
	}
}


int dvbpush_command(int cmd, char **buf, int *len)
{
	int ret = 0;

	DEBUG("command: %d=0x%x\n", cmd,cmd);
	memset(s_jni_cmd_public_space,0,sizeof(s_jni_cmd_public_space));
	
	switch (cmd) {
		case CMD_DVBPUSH_GETINFO_START:
			dvbpush_getinfo_start();
			break;
		case CMD_DVBPUSH_GETINFO:
			dvbpush_getinfo(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		case CMD_DVBPUSH_GETINFO_STOP:
			dvbpush_getinfo_stop();
			break;
		case CMD_NETWORK_CONNECT:
		case CMD_NETWORK_DISCONNECT:
		case CMD_DISK_MOUNT:
		case CMD_DISK_UNMOUNT:
			net_rely_condition_set(cmd);
			break;
		case CMD_DVBPUSH_GETTS_STATUS:
			data_stream_status_str_get(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		
		case CMD_UPGRADE_CANCEL:
			DEBUG("CMD_UPGRADE_CANCEL\n");
			upgrade_sign_set();
			break;
		case CMD_PUSH_SELECT:
			DEBUG("CMD_PUSH_SELECT: GuideList selected by user\n");
			guidelist_select_refresh();
			break;
		
		case CMD_DISK_FOREWARNING:
			DEBUG("CMD_DISK_FOREWARNING: Disk alarm for capability\n");
			disk_manage_flag_set(1);
			break;
		
		case CMD_DRM_SC_INSERT:
			DEBUG("CMD_SMARTCARD_INSERT\n");
			if(-1==drm_sc_insert())
				msg_send2_UI(DRM_SC_INSERT_FAILED, NULL, 0);
#if 0
// drm_sc_insert调用成功并不意味着智能卡复位成功，因此成功的信号不在这里发送。
			else
				msg_send2_UI(DRM_SC_INSERT_OK, NULL, 0);
#else
			DEBUG("call drm_sc_insert success, but it not means reset card OK, wait a moment...\n");
#endif				
			break;
		case CMD_DRM_SC_REMOVE:
			DEBUG("CMD_SMARTCARD_REMOVE\n");
			smart_card_insert_flag_set(0);
			smart_card_remove_flag_set(1);
			if(-1==drm_sc_remove())
				msg_send2_UI(DRM_SC_REMOVE_FAILED, NULL, 0);
			else
				msg_send2_UI(DRM_SC_REMOVE_OK, NULL, 0);
			
			break;
		case CMD_DRM_SC_SN_READ:
			DEBUG("CMD_DRM_SC_SN_READ\n");
			smartcard_sn_get(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		case CMD_DRMLIB_VER_READ:
			DEBUG("CMD_DRMLIB_VER_READ\n");
			drmlib_version_get(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		case CMD_DRM_SC_EIGENVALUE_READ:
			DEBUG("CMD_DRM_SC_EIGENVALUE_READ\n");
			smartcard_eigenuvalue_get(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		case CMD_DRM_ENTITLEINFO_READ:
			DEBUG("CMD_DRM_ENTITLEINFO_READ\n");
			smartcard_entitleinfo_get(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		case CMD_DRM_ENTITLEINFO_OUTPUT:
			DEBUG("CMD_DRM_ENTITLEINFO_OUTPUT\n");
			smartcard_EntitleFile_output(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			DEBUG("CMD_DRM_ENTITLEINFO_OUTPUT > %s\n", s_jni_cmd_public_space);
			break;
		case CMD_DRM_ENTITLEINFO_INPUT:
			DEBUG("CMD_DRM_ENTITLEINFO_INPUT\n");
			smartcard_EntitleFile_input(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			DEBUG("CMD_DRM_ENTITLEINFO_INPUT > %s\n", s_jni_cmd_public_space);
			break;
		case CMD_DRM_EMAILHEADS_READ:
			DEBUG("CMD_DRM_EMAILHEADS_READ\n");
			DRM_emailheads_get(s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		case CMD_DRM_EMAILCONTENT_READ:
			DEBUG("CMD_DRM_EMAILCONTENT_READ\n");
			DRM_emailcontent_get(*buf,s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		case CMD_DRM_PVODPROGRAMINFO_READ:
			DEBUG("CMD_DRM_PVODPROGRAMINFO_READ\n");
			DRM_programinfo_get(*buf,s_jni_cmd_public_space,sizeof(s_jni_cmd_public_space));
			break;
		default:
			DEBUG("can not distinguish such cmd %d=0x%x\n", cmd,cmd);
			break;
	}
	
	*buf = s_jni_cmd_public_space;
	*len = strlen(s_jni_cmd_public_space);

	return ret;
}


static void upgrade_info_refresh(char *info_name, char *info_value)
{
	char sqlite_cmd[512];
	char stbinfo[128];
	
	int (*sqlite_cb)(char **, int, int, void *, unsigned int) = str_read_cb;
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value FROM Global WHERE Name='%s';", info_name);
	
	memset(stbinfo, 0, sizeof(stbinfo));
	int ret_sqlexec = sqlite_read(sqlite_cmd, stbinfo, sizeof(stbinfo), sqlite_cb);
	
	if(ret_sqlexec<=0 || strcmp(stbinfo, info_value)){
		DEBUG("replace %s as %s to table 'Global'\n", info_name, info_value);
		snprintf(sqlite_cmd, sizeof(sqlite_cmd), "REPLACE INTO Global(Name,Value,Param) VALUES('%s','%s','');",
			info_name,info_value);
		sqlite_execute(sqlite_cmd);
	}
	else
		DEBUG("same %s: %s\n", info_name, info_value);
}

/*
 0:		normal upgrade
 255:	repeat upgrade
*/
static int upgrade_type_check( unsigned char *software_version)
{
	DEBUG("%03d.%03d.%03d.%03d\n",software_version[1],software_version[2],software_version[2],software_version[3]);
	if(255==software_version[0] && 255==software_version[1]
		&& 255==software_version[2] && 255==software_version[3]){
		DEBUG("It has finish a repeat upgrade\n");
		return 255;
	}
	else{
		DEBUG("It has finish a normal upgrade\n");
		return 0;
	}
}

void upgrade_info_init()
{
	unsigned char mark = 0;
	char tmpinfo[128];
	
	char sqlite_cmd[256];
	char repeat_upgrade_count[8];
	
	extern LoaderInfo_t g_loaderInfo;
	
	memset(&g_loaderInfo, 0, sizeof(g_loaderInfo));
	if(0==get_loader_message(&mark, &g_loaderInfo))
	{
		DEBUG("read loader msg: %d", mark);
		
		if(255==upgrade_type_check(g_loaderInfo.software_version)){
			memset(repeat_upgrade_count,0,sizeof(repeat_upgrade_count));
			snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value from Global where Name='RepeatUpgradeCount';");
			if(-1==str_sqlite_read(repeat_upgrade_count,sizeof(repeat_upgrade_count),sqlite_cmd)){
				DEBUG("can not read RepeatUpgradeCount\n");
			}
			else{
				DEBUG("read RepeatUpgradeCount: %s\n", repeat_upgrade_count);
			}
		}
		
		if(0!=mark){
			DEBUG("clear upgrade mark and file\n");
			set_loader_reboot_mark(0);
			upgradefile_clear();
			
			if(255==upgrade_type_check(g_loaderInfo.software_version)){
				snprintf(tmpinfo,sizeof(tmpinfo),"%d", atoi(repeat_upgrade_count)+1);
				upgrade_info_refresh("RepeatUpgradeCount", tmpinfo);
			}
		}
		
#if 0
		if(255==upgrade_type_check(g_loaderInfo.software_version)){
			snprintf(msg_2_UI,sizeof(msg_2_UI),"Repeat upgrade count: %s\n", tmpinfo);
			msg_send2_UI(DIALOG_NOTICE, msg_2_UI, strlen(msg_2_UI));
		}
#endif
		
		snprintf(tmpinfo, sizeof(tmpinfo), "%08u%08u", g_loaderInfo.stb_id_h,g_loaderInfo.stb_id_l);
		upgrade_info_refresh(GLB_NAME_PRODUCTSN, tmpinfo);
		DEBUG("stb id: %s\n", tmpinfo);

#if 0
		snprintf(tmpinfo, sizeof(tmpinfo), "%03d.%03d.%03d.%03d", g_loaderInfo.hardware_version[0],g_loaderInfo.hardware_version[1],g_loaderInfo.hardware_version[2],g_loaderInfo.hardware_version[3]);
		upgrade_info_refresh(GLB_NAME_HARDWARE_VERSION, tmpinfo);
		
		snprintf(tmpinfo, sizeof(tmpinfo), "%03d.%03d.%03d.%03d", g_loaderInfo.software_version[0],g_loaderInfo.software_version[1],g_loaderInfo.software_version[2],g_loaderInfo.software_version[3]);
		upgrade_info_refresh(GLB_NAME_SOFTWARE_VERSION, tmpinfo);
		upgrade_info_refresh(GLB_NAME_LOADER_VERSION, tmpinfo);
#else		
/*
下面三行才是航天传媒定义的显示在本地配置的版本号，其中：
1、硬件版本号在同一批产品中不变，固定为“03.01”；
2、软件版本号的前两段为2.0，第3段为大的功能版本号，第4段为提交的轮次；
3、Loader没有独立的版本号，直接使用默认的版本号“1.2.1”，其中前两段“1.2”为固定，最后一段为版本轮次；
4、设备型号固定使用分配的“01”
*/
		upgrade_info_refresh(GLB_NAME_HARDWARE_VERSION, HARDWARE_VERSION);
		
		snprintf(tmpinfo, sizeof(tmpinfo), "2.0.%d.%d", g_loaderInfo.software_version[2],g_loaderInfo.software_version[3]);
		upgrade_info_refresh(GLB_NAME_SOFTWARE_VERSION, tmpinfo);

		upgrade_info_refresh(GLB_NAME_LOADER_VERSION, LOADER_VERSION);		
		upgrade_info_refresh(GLB_NAME_DEVICEMODEL, DEVICEMODEL_DFT);
#endif
	}
	else
		DEBUG("get loader message failed\n");
}

//int drm_info_refresh()
//{
//	if(0==drm_init()){
//		char		smartcard_sn[CDCA_MAXLEN_SN+1];
//		CDCA_U16	wArrTvsID[CDCA_MAXNUM_OPERATOR];
//		CDCA_U8		bySlotID[CDCA_MAXNUM_SLOT];
//		CDCA_U32	ACArray[CDCA_MAXNUM_ACLIST];
//		CDCA_U8		index = 0;
//		CDCA_U16	j = 0;
//		SCDCAEntitles Entitle;
//		CDCA_U16	ret = CDCA_RC_OK;
//		CDCA_U16	OperatorCount = 0;
//		SCDCAOperatorInfo OperatorInfo;
//		char		BeginDate[64];
//		char		ExpireDate[64];
//		
///*
// 查询CA_LIB版本号，要求机顶盒以16进制显示
//*/
//		DEBUG("CA_LIB Ver: 3.0(0x%lx)\n", CDCASTB_GetVer());
//		
///*
// 智能卡号
//*/
//		memset(smartcard_sn, 0, sizeof(smartcard_sn));
//		ret = CDCASTB_GetCardSN(smartcard_sn);
//		if(CDCA_RC_OK==ret){
//			DEBUG("read smartcard sn OK: %s\n", smartcard_sn);
//		}
//		else{
//			drm_errors("CDCASTB_GetCardSN", ret);
//			return -1;
//		}
//
///*
// 运营商ID列表
//*/		
//		memset(wArrTvsID, 0, sizeof(wArrTvsID));
//		ret = CDCASTB_GetOperatorIds(wArrTvsID);
//		if(CDCA_RC_OK==ret){
//			for(index=0;index<CDCA_MAXNUM_OPERATOR;index++){
//				if(0==wArrTvsID[index]){
//					//DEBUG("OperatorID list end\n");
//					break;
//				}
//				else
//					DEBUG("OperatorID: %d\n", wArrTvsID[index]);
//			}
//		}
//		else{
//			drm_errors("CDCASTB_GetOperatorIds", ret);
//			return -1;
//		}
//		
//		OperatorCount = index;
//		for(j=0;j<OperatorCount;j++){
///*
// 运营商信息
//*/
//			ret = CDCASTB_GetOperatorInfo(wArrTvsID[j], &OperatorInfo);
//			if(CDCA_RC_OK==ret){
//				DEBUG("[%d]: %s\n", wArrTvsID[j], OperatorInfo.m_szTVSPriInfo);
//			}
//			else
//				drm_errors("CDCASTB_GetServiceEntitles", ret);
//			
///*
// 普通授权节目购买情况
//*/
//			memset(&Entitle, 0, sizeof(Entitle));
//			ret = CDCASTB_GetServiceEntitles(wArrTvsID[j],&Entitle);
//			if(CDCA_RC_OK==ret){
//				DEBUG("Operator %d has %d entitles\n", wArrTvsID[j],Entitle.m_wProductCount);
//				for(index=0;index<Entitle.m_wProductCount;index++){
//					memset(BeginDate, 0, sizeof(BeginDate));
//					memset(ExpireDate, 0, sizeof(ExpireDate));
//					if(		0==drm_time_convert(Entitle.m_Entitles[index].m_tBeginDate, BeginDate, sizeof(BeginDate))
//						&& 	0==drm_time_convert(Entitle.m_Entitles[index].m_tExpireDate, ExpireDate, sizeof(ExpireDate))){
//						DEBUG("[Operator %d]Product id: %lu, Product Expire: %s-%s, CanTape: %d\n", wArrTvsID[j],Entitle.m_Entitles[index].m_dwProductID,
//							BeginDate,ExpireDate,Entitle.m_Entitles[index].m_bCanTape);
//					}
//				}
//			}
//			else{
//				drm_errors("CDCASTB_GetServiceEntitles", ret);
//				return -1;
//			}
//			
///*
// 查询钱包ID列表
//*/
//			memset(bySlotID, 0, sizeof(bySlotID));
//			ret = CDCASTB_GetSlotIDs(wArrTvsID[j], bySlotID);
//			if(CDCA_RC_OK==ret){
//				SCDCATVSSlotInfo SlotInfo;
//				for(index=0;index<CDCA_MAXNUM_SLOT;index++){
///*
// 查询钱包的详细信息
//*/
//					memset(&SlotInfo, 0, sizeof(SlotInfo));
//					ret = CDCASTB_GetSlotInfo(wArrTvsID[j],bySlotID[index],&SlotInfo);
//					if(CDCA_RC_OK==ret)
//						DEBUG("bySlotID[%d]: %d, CreditLimit:%lu, Balance:%lu\n", index, bySlotID[index],SlotInfo.m_wCreditLimit, SlotInfo.m_wBalance);
//					else{
//						char tmp_str[128];
//						snprintf(tmp_str, sizeof(tmp_str), "CDCASTB_GetSlotInfo Operator: %d, bySlotID: %d", wArrTvsID[j],bySlotID[index]);
//						drm_errors(tmp_str, ret);
//					}
//				}
//			}
//			else
//				drm_errors("CDCASTB_GetSlotIDs", ret);
//			
///*
// 查询用户特征
//*/
//			memset(ACArray, 0, sizeof(ACArray));
//			ret = CDCASTB_GetACList(wArrTvsID[j], ACArray);
//			if(CDCA_RC_OK==ret){
//				for(index=0;index<CDCA_MAXNUM_ACLIST;index++)
//					if(0!=ACArray[index])
//						DEBUG("Operator: %d, ACArray[%d]:%lu\n", wArrTvsID[j],index,ACArray[index]);
//			}
//			else
//				drm_errors("CDCASTB_GetACList", ret);
//		}
//		
///*
// 查询授权信息
//*/
//		CDCA_U32 dwFrom = 0, dwNum = 0;
//		SCDCAPVODEntitleInfo EntitleInfo;
//		memset(&EntitleInfo, 0, sizeof(EntitleInfo));
//		ret = CDCASTB_DRM_GetEntitleInfo(&dwFrom,&EntitleInfo,&dwNum);
//		if(CDCA_RC_OK==ret)
//			DEBUG("dwFrom=%lu, dwNum=%lu\n", dwFrom, dwNum);
//		else
//			drm_errors("CDCASTB_DRM_GetEntitleInfo", ret);
//	}
//	else
//		DEBUG("drm init failed\n");
//	
//	
//	return -1;
//}

/*
struct tm{
int tm_sec;
int tm_min;
int tm_hour;
int tm_mday;
int tm_mon;//代表目前月份，从一月算起，范围从0-11
int tm_year;
int tm_wday;
int tm_yday;
int tm_isdst;
};
*/
/*
void drm_date_time_test()
{
//	time_t timep;
//	struct tm *p;
//	time(&timep);
//	printf(“time() : %d n”,timep);
//	p=localtime(&timep);
//	timep = mktime(p);
//	printf(“time()->localtime()->mktime():%dn”,timep);
	
	time_t sec_2000;
	struct tm *p;
	struct tm tm_2000;
	memset(&tm_2000, 0, sizeof(struct tm));
	tm_2000.tm_mday = 1;
	tm_2000.tm_mon = 0;
	tm_2000.tm_year = (2000-1900);
	sec_2000 = mktime(&tm_2000);
	p = localtime(&sec_2000);
	DEBUG("%dYear %dMon %dDay: %dHour %dMin %dSec\n", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
	sec_2000 += (13879*24*60*60);
	p = localtime(&sec_2000);
	DEBUG("%dYear %dMon %dDay: %dHour %dMin %dSec\n", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
	
	time_t sec_today;
	struct tm tm_today;
	memset(&tm_today, 0, sizeof(struct tm));
	tm_today.tm_sec = 27;
	tm_today.tm_min = 59;
	tm_today.tm_hour = 14;
	tm_today.tm_mday = 28;
	tm_today.tm_mon = 9;
	tm_today.tm_year = (2012-1900);
	sec_today = mktime(&tm_today);
	p = localtime(&sec_today);
	DEBUG("%dYear %dMon %dDay: %dHour %dMin %dSec\n", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
	
	sec_today += (7*24*60*60);
	p = localtime(&sec_today);
	DEBUG("%dYear %dMon %dDay: %dHour %dMin %dSec\n", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
}
*/

/*
 参考drm_date_time_test实现，将drm中的date转换为年月日。date为自2000年1月1日开始的天数，详见drm移植文档。
*/
static int drm_time_convert(unsigned int drm_time, char *date_str, unsigned int date_str_size)
{
	if(NULL==date_str || 0==date_str_size){
		DEBUG("invalid args\n");
		return -1;
	}
	
	time_t sec_appointed;
	struct tm tm_appointed;
	struct tm *p;
	
	memset(&tm_appointed, 0, sizeof(struct tm));
	tm_appointed.tm_mday = 1;
	tm_appointed.tm_mon = 0;
	tm_appointed.tm_year = (2000-1900);
	tm_appointed.tm_isdst = 0;
	sec_appointed = mktime(&tm_appointed);
	
	p = localtime(&sec_appointed);
	//DEBUG("%dYear %dMon %dDay: %dHour %dMin %dSec\n", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
	
	unsigned int drm_date	= ((drm_time & 0xffff0000)>>16);
	unsigned int drm_hour	= ((drm_time & 0x0000f800)>>11);
	unsigned int drm_min	= ((drm_time & 0x000007e0)>>5);
	unsigned int drm_2secs	= (drm_time & 0x0000001f);
	
	DEBUG("drm_time=%u, drm_date=%u, drm_hour=%u, drm_min=%u, drm_2secs=%u\n", drm_time,drm_date,drm_hour,drm_min, drm_2secs);
	
	sec_appointed += ((drm_date*24*60*60)+(drm_hour*60*60)+(drm_min*60)+(drm_2secs*2));
	
	p = localtime(&sec_appointed);
	snprintf(date_str, date_str_size, "%04d-%02d-%02d %02d:%02d:%02d", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
	
	DEBUG("origine drm_time=%u, trans as %s\n", drm_time,date_str);
	
	return 0;
}

static int cur_language_init()
{
	if(0==strlen(s_Language)){
		char sqlite_cmd[512];
		
		int (*sqlite_cb)(char **, int, int, void *, unsigned int) = str_read_cb;
		snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value FROM Global WHERE Name='%s';", GLB_NAME_CURLANGUAGE);
	
		int ret_sqlexec = sqlite_read(sqlite_cmd, s_Language, sizeof(s_Language), sqlite_cb);
		if(ret_sqlexec<=0){
			DEBUG("read no Language from db, filled with %s\n", CURLANGUAGE_DFT);
			snprintf(s_Language, sizeof(s_Language), "%s", CURLANGUAGE_DFT);
		}
		else
			DEBUG("read Language: %s\n", s_Language);
	}
	
	return 0;
}

char *language_get()
{
	if(0==strlen(s_Language)){
		char sqlite_cmd[512];
		
		int (*sqlite_cb)(char **, int, int, void *, unsigned int) = str_read_cb;
		snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value FROM Global WHERE Name='%s';", GLB_NAME_CURLANGUAGE);
	
		int ret_sqlexec = sqlite_read(sqlite_cmd, s_Language, sizeof(s_Language), sqlite_cb);
		if(ret_sqlexec<=0){
			DEBUG("read no Language from db, filled with %s\n", CURLANGUAGE_DFT);
			snprintf(s_Language, sizeof(s_Language), "%s", CURLANGUAGE_DFT);
		}
		else
			DEBUG("read Language: %s\n", s_Language);
	}
	
	return s_Language;
}

char *multi_addr_get(void)
{
	char sqlite_cmd[512];
	char read_str[512];
	
	memset(read_str, 0, sizeof(read_str));
	int (*sqlite_cb)(char **, int, int, void *, unsigned int) = str_read_cb;
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value FROM Global WHERE Name='%s';", GLB_NAME_DBDATASERVERIP);
	int ret_sqlexec = sqlite_read(sqlite_cmd, read_str, sizeof(read_str), sqlite_cb);
	if(ret_sqlexec<=0){
		DEBUG("read nothing for multi ip, filled with default\n");
		snprintf(s_data_source, sizeof(s_data_source), "igmp://%s", DBDATASERVERIP_DFT);
	}
	else{
		snprintf(s_data_source, sizeof(s_data_source), "igmp://%s", read_str);
	}
	DEBUG("multi ip: %s\n", read_str);
	
	memset(read_str, 0, sizeof(read_str));
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value FROM Global WHERE Name='%s';", GLB_NAME_DBDATASERVERPORT);
	ret_sqlexec = sqlite_read(sqlite_cmd, read_str, sizeof(read_str), sqlite_cb);
	if(ret_sqlexec<=0){
		DEBUG("read nothing for multi port, filled with default\n");
		snprintf(s_data_source+strlen(s_data_source), sizeof(s_data_source)-strlen(s_data_source), ":%s", DBDATASERVERPORT_DFT);
	}
	else{
		snprintf(s_data_source+strlen(s_data_source), sizeof(s_data_source)-strlen(s_data_source), ":%s", read_str);
	}
	DEBUG("multi addr: %s\n", s_data_source);
	
	return s_data_source;
}


static int serviceID_init()
{
	char sqlite_cmd[512];
	memset(s_serviceID, 0, sizeof(s_serviceID));
	int (*sqlite_cb)(char **, int, int, void *,unsigned int) = str_read_cb;
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value FROM Global WHERE Name='%s';", GLB_NAME_SERVICEID);

	int ret_sqlexec = sqlite_read(sqlite_cmd, s_serviceID, sizeof(s_serviceID), sqlite_cb);
	if(ret_sqlexec<=0){
		DEBUG("read no serviceID from db\n");
	}
	else
		DEBUG("read serviceID: %s\n", s_serviceID);
	
	return 0;
}

char *serviceID_get()
{
	return s_serviceID;
}

/*
 仅set到全局变量中。至于数据库，则是在xml解析时在“数据库事务”设置的。
 由于此调用处在“事务”中，所以如果确实需要写入数据库，也需要调用“事务”中的数据库操作
*/
int serviceID_set(char *serv_id)
{
	return snprintf(s_serviceID,sizeof(s_serviceID),"%s",serv_id);
}



/*
 从数据表Global中读取push的根路径，此路径由上层写入数据库。
 此路径应当更新到push.conf中供push模块初始化使用。
 之所以这么更新，是因为无法确保硬盘一定是挂在/mnt/sda1下。
*/
char *push_dir_get()
{
	return s_push_root_path;
}

static int push_dir_init()
{
	char sqlite_cmd[512];
	
	memset(s_push_root_path, 0, sizeof(s_push_root_path));
	
	int (*sqlite_cb)(char **, int, int, void *, unsigned int) = str_read_cb;
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value FROM Global WHERE Name='%s';", GLB_NAME_PUSHDIR);

	int ret_sqlexec = sqlite_read(sqlite_cmd, s_push_root_path, sizeof(s_push_root_path), sqlite_cb);
	if(ret_sqlexec<=0 || strlen(s_push_root_path)<2){
		DEBUG("read no PushDir from db, filled with %s\n", PUSH_DATA_DIR_DF);
		snprintf(s_push_root_path, sizeof(s_push_root_path), "%s", PUSH_DATA_DIR_DF);
	}
	else
		DEBUG("read PushDir: %s\n", s_push_root_path);
		
	return 0;
}

// 联调测试阶段，智能卡数量不够，这里直接指定业务ID。正式运营的盒子不能执行到这里
static int TestSpecialProductID_init()
{
// only for test
	char sqlite_cmd[256];
	memset(s_TestSpecialProductID, 0, sizeof(s_TestSpecialProductID));
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT Value from Global where Name='TestSpecialProductID';");
	if(-1==str_sqlite_read(s_TestSpecialProductID,sizeof(s_TestSpecialProductID),sqlite_cmd)){
		DEBUG("can not read s_TestSpecialProductID\n");
		return -1;
	}
	else{
		DEBUG("read s_TestSpecialProductID: %s\n", s_TestSpecialProductID);
	}
	
	return 0;
}
/*
typedef struct {
    CDCA_U16   m_OperatorID;
    CDCA_U32   m_ID;
    CDCA_TIME  m_ProductStartTime;
    CDCA_TIME  m_ProductEndTime;
    CDCA_TIME  m_WatchStartTime;
    CDCA_TIME  m_WatchEndTime;
    CDCA_U32   m_LimitTotaltValue;
    CDCA_U32   m_LimitUsedValue;
}SCDCAPVODEntitleInfo;
*/
typedef struct{
	char SmartCardID[64];
	SCDCAPVODEntitleInfo EntitleInfo;
}SCENTITLEINFO;

#define SCENTITLEINFOSIZE	(128)
static SCENTITLEINFO s_SCEntitleInfo[SCENTITLEINFOSIZE];

static int SCEntitleInfo_init_cb(char **result, int row, int column, void *receiver, unsigned int receiver_size)
{
	DEBUG("sqlite callback, row=%d, column=%d, receiver addr=%p, receive_size=%u\n", row, column, receiver,receiver_size);
	if(row<1){
		DEBUG("no record in table, return\n");
		return 0;
	}
	
	int i = 0;
	
// SmartCardID,m_OperatorID,m_ID,m_ProductStartTime,m_ProductEndTime,m_WatchStartTime,m_WatchEndTime,m_LimitTotaltValue,m_LimitUsedValue
	for(i=1;i<SCENTITLEINFOSIZE+1;i++)
	{
		if(i<(row+1)){
			snprintf(s_SCEntitleInfo[i-1].SmartCardID,sizeof(s_SCEntitleInfo[i-1].SmartCardID),"%s", result[i*column]);
			s_SCEntitleInfo[i-1].EntitleInfo.m_OperatorID = strtoul(result[i*column+1],NULL,0);
			s_SCEntitleInfo[i-1].EntitleInfo.m_ID = strtoul(result[i*column+2],NULL,0);
			s_SCEntitleInfo[i-1].EntitleInfo.m_ProductStartTime = strtoul(result[i*column+3],NULL,0);
			s_SCEntitleInfo[i-1].EntitleInfo.m_ProductEndTime = strtoul(result[i*column+4],NULL,0);
			s_SCEntitleInfo[i-1].EntitleInfo.m_WatchStartTime = strtoul(result[i*column+5],NULL,0);
			s_SCEntitleInfo[i-1].EntitleInfo.m_WatchEndTime = strtoul(result[i*column+6],NULL,0);
			s_SCEntitleInfo[i-1].EntitleInfo.m_LimitTotaltValue = strtoul(result[i*column+7],NULL,0);
			s_SCEntitleInfo[i-1].EntitleInfo.m_LimitUsedValue = strtoul(result[i*column+8],NULL,0);
			
			DEBUG("[%d]%s\t%u\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu", i-1,s_SCEntitleInfo[i-1].SmartCardID,s_SCEntitleInfo[i-1].EntitleInfo.m_OperatorID,
				s_SCEntitleInfo[i-1].EntitleInfo.m_ProductStartTime,s_SCEntitleInfo[i-1].EntitleInfo.m_ProductEndTime,
				s_SCEntitleInfo[i-1].EntitleInfo.m_WatchStartTime,s_SCEntitleInfo[i-1].EntitleInfo.m_WatchEndTime,
				s_SCEntitleInfo[i-1].EntitleInfo.m_LimitTotaltValue,s_SCEntitleInfo[i-1].EntitleInfo.m_LimitUsedValue);
		}
		else
			s_SCEntitleInfo[i-1].EntitleInfo.m_ID = INVALID_PRODUCTID_AT_ENTITLEINFO;
	}
	
	return 0;
}

static int SCEntitleInfo_init(void)
{
	char sqlite_cmd[256];
	int (*sqlite_callback)(char **, int, int, void *, unsigned int) = SCEntitleInfo_init_cb;
	
	int i = 0;
	for(i=0;i<SCENTITLEINFOSIZE;i++)
	{
		s_SCEntitleInfo[i].EntitleInfo.m_ID = INVALID_PRODUCTID_AT_ENTITLEINFO;
	}
	
	snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT SmartCardID,m_OperatorID,m_ID,m_ProductStartTime,m_ProductEndTime,m_WatchStartTime,m_WatchEndTime,m_LimitTotaltValue,m_LimitUsedValue from SCEntitleInfo;");
	int ret = sqlite_read(sqlite_cmd, NULL, 0, sqlite_callback);
	DEBUG("read %d record for SmartCard Entitle Info in database\n", ret);	
	
	return 0;
}

int intialize_xml_reset(void)
{
	// 如果是插入智能卡，需要和数据表SCEntitleInfo比对其特殊产品是否有变化，以此判断是否是更换了智能卡
	if(0==strlen(s_serviceID) || 1==smartcard_entitleinfo_refresh()){
		DEBUG("\n\n\n\n\n\n\n\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n\ndo xmls reset\n\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n\n\n\n\n\n\n\n\n\n");
		
		char sqlite_cmd[256];
		char initialize_xml_uri[512];
		char total_xmluri[512];
		
// 1、停止现有正在接收的节目
		prog_monitor_reset();
		
// 2、删除播发单ProductDesc表
		snprintf(sqlite_cmd, sizeof(sqlite_cmd), "DELETE FROM ProductDesc;");
		sqlite_execute(sqlite_cmd);
		
// 3、清理Initialize.xml和Initialize表，使得初始化文件再次下发，重新判断ServiceID
// 		旧xml已经在push中注册，这里就不反注册并清理了，减少逻辑复杂度。执行到这里的概率很小
		
		memset(initialize_xml_uri, 0, sizeof(initialize_xml_uri));
		snprintf(sqlite_cmd,sizeof(sqlite_cmd),"SELECT URI FROM Initialize WHERE PushFlag='%d';", INITIALIZE_XML);
		if(-1==str_sqlite_read(initialize_xml_uri,sizeof(initialize_xml_uri),sqlite_cmd)){
			snprintf(total_xmluri,sizeof(total_xmluri),"%s/pushroot/initialize", push_dir_get());
			DEBUG("can not read initialize_xml_uri, remove %s instead of\n", total_xmluri);
		}
		else{
			DEBUG("read initialize_xml_uri: %s\n", initialize_xml_uri);
			snprintf(total_xmluri,sizeof(total_xmluri),"%s/%s", push_dir_get(),initialize_xml_uri);
		}
		remove_force(total_xmluri);
		DEBUG("remove %s\n", total_xmluri);
		
		snprintf(sqlite_cmd,sizeof(sqlite_cmd), "DELETE FROM Initialize;");
		sqlite_execute(sqlite_cmd);
		
		return 0;
	}
	else
		DEBUG("already have s_serviceID: %s, or smart card has not refresh\n", s_serviceID);
	
	return 0;
}

static unsigned int SCEntitleInfoNum_get(void)
{
	int i = 0;
	for(i=0;i<SCENTITLEINFOSIZE;i++){
		if(	s_SCEntitleInfo[i].EntitleInfo.m_ID<=INVALID_PRODUCTID_AT_ENTITLEINFO){
			//DEBUG("s_SCEntitleInfo[%d] is invalid\n", i);
			break;
		}
//		else
//			DEBUG("s_SCEntitleInfo[%d]=%lu is valid\n", i,s_SCEntitleInfo[i].EntitleInfo.m_ID);
	}
	
	DEBUG("SCEntitleInfoNum_get() = %u\n", (unsigned int)i);
	return (unsigned int)i;
}

static int SCEntitleInfoCheck(SCDCAPVODEntitleInfo *EntitleInfo)
{
	if(NULL==EntitleInfo){
		DEBUG("invalid arg\n");
		return -1;
	}

	int i = 0;
	for(i=0;i<SCENTITLEINFOSIZE;i++){
#if 0
		if(		EntitleInfo->m_OperatorID==s_SCEntitleInfo[i].EntitleInfo.m_OperatorID
			&&	EntitleInfo->m_ID==s_SCEntitleInfo[i].EntitleInfo.m_ID
			&&	EntitleInfo->m_ProductStartTime==s_SCEntitleInfo[i].EntitleInfo.m_ProductStartTime
			&&	EntitleInfo->m_ProductEndTime==s_SCEntitleInfo[i].EntitleInfo.m_ProductEndTime
			&&	EntitleInfo->m_WatchStartTime==s_SCEntitleInfo[i].EntitleInfo.m_WatchStartTime
			&&	EntitleInfo->m_LimitTotaltValue==s_SCEntitleInfo[i].EntitleInfo.m_LimitTotaltValue
			&&	EntitleInfo->m_LimitUsedValue==s_SCEntitleInfo[i].EntitleInfo.m_LimitUsedValue){
			DEBUG("check equal record at: %d\n",i);
			return 0;
		}
#else
		if(	EntitleInfo->m_ID==s_SCEntitleInfo[i].EntitleInfo.m_ID){
			DEBUG("check equal record at %d for %lu\n",i,EntitleInfo->m_ID);
			return 0;
		}
#endif
	}
	
	return -1;
}

/*
 return: 
 	-1:	failed
 	0:	success and no need refresh
 	1:	success and need refresh
*/
static int smartcard_entitleinfo_refresh()
{
	int ret = -1;
	CDCA_U32 dwFrom = 0, dwNum = 128;
	unsigned int i = 0;
	char SmartCardSn[128];
	SCDCAPVODEntitleInfo EntitleInfo[128];
	int SC_EntitleInfo_fresh = 0;
	char sqlite_cmd[1024];
	
	memset(SmartCardSn,0,sizeof(SmartCardSn));
	ret = CDCASTB_GetCardSN(SmartCardSn);
	if(CDCA_RC_OK==ret){
		DEBUG("read smartcard sn OK: %s\n", SmartCardSn);
		ret = 0;
		
/*
 查询授权信息
*/
		int ret = CDCASTB_DRM_GetEntitleInfo(&dwFrom,EntitleInfo,&dwNum);
		if(CDCA_RC_OK==ret){
			DEBUG("dwFrom=%lu, dwNum=%lu\n", dwFrom, dwNum);
			if(	dwNum==SCEntitleInfoNum_get()){
				DEBUG("dwNum is equal, check details continue...\n");
				SC_EntitleInfo_fresh = 0;
				for(i=0;i<dwNum;i++){
					if(-1==SCEntitleInfoCheck(&EntitleInfo[i])){
						SC_EntitleInfo_fresh = 1;
						break;
					}
				}
			}
			else
				SC_EntitleInfo_fresh = 1;
			
			if(1==SC_EntitleInfo_fresh){
				DEBUG("refresh smart card entitle infos to memery and database\n");
				snprintf(sqlite_cmd, sizeof(sqlite_cmd), "DELETE FROM SCEntitleInfo;");
				sqlite_execute(sqlite_cmd);
				
				int sqlite_transaction_flag = sqlite_transaction_begin();
				
				for(i=0;i<SCENTITLEINFOSIZE;i++){
					if(i<dwNum)	// this should be a valid Entitle info
					{
						snprintf(s_SCEntitleInfo[i].SmartCardID,sizeof(s_SCEntitleInfo[i].SmartCardID),"%s",SmartCardSn);
						s_SCEntitleInfo[i].EntitleInfo.m_OperatorID = EntitleInfo[i].m_OperatorID;
						s_SCEntitleInfo[i].EntitleInfo.m_ID = EntitleInfo[i].m_ID;
						s_SCEntitleInfo[i].EntitleInfo.m_ProductStartTime = EntitleInfo[i].m_ProductStartTime;
						s_SCEntitleInfo[i].EntitleInfo.m_ProductEndTime = EntitleInfo[i].m_ProductEndTime;
						s_SCEntitleInfo[i].EntitleInfo.m_WatchStartTime = EntitleInfo[i].m_WatchStartTime;
						s_SCEntitleInfo[i].EntitleInfo.m_WatchEndTime = EntitleInfo[i].m_WatchEndTime;
						s_SCEntitleInfo[i].EntitleInfo.m_LimitTotaltValue = EntitleInfo[i].m_LimitTotaltValue;
						s_SCEntitleInfo[i].EntitleInfo.m_LimitUsedValue = EntitleInfo[i].m_LimitUsedValue;
						
						if(0==sqlite_transaction_flag){
							snprintf(sqlite_cmd, sizeof(sqlite_cmd), "REPLACE INTO SCEntitleInfo(SmartCardID,m_OperatorID,m_ID,m_ProductStartTime,m_ProductEndTime,m_WatchStartTime,m_WatchEndTime,m_LimitTotaltValue,m_LimitUsedValue) VALUES('%s','%u','%lu','%lu','%lu','%lu','%lu','%lu','%lu');",
								SmartCardSn,EntitleInfo[i].m_OperatorID,EntitleInfo[i].m_ID,EntitleInfo[i].m_ProductStartTime,EntitleInfo[i].m_ProductEndTime,EntitleInfo[i].m_WatchStartTime,EntitleInfo[i].m_WatchEndTime,EntitleInfo[i].m_LimitTotaltValue,EntitleInfo[i].m_LimitUsedValue);
							sqlite_transaction_exec(sqlite_cmd);
						}
					}
					else
						s_SCEntitleInfo[i].EntitleInfo.m_ID = 0;
				}
				
				if(0==sqlite_transaction_flag)
					sqlite_transaction_end(1);
				
				return 1;
			}
			else{
				DEBUG("this is a card with familiar Special Product, no need to refresh entitle infos\n");
				return 0;
			}
		}
		else{
			drm_errors("CDCASTB_DRM_GetEntitleInfo", ret);
			return -1;
		}
	}
	else{
		drm_errors("CDCASTB_GetCardSN", ret);
		return -1;
	}
}

int smart_card_insert_flag_set(int insert_flag)
{
	s_smart_card_insert_flag = insert_flag;
	DEBUG("s_smart_card_insert_flag=%d, s_smart_card_remove_flag=%d\n", s_smart_card_insert_flag,s_smart_card_remove_flag);
	
	return s_smart_card_insert_flag;
}

int smart_card_insert_flag_get()
{
	return s_smart_card_insert_flag;
}

int smart_card_remove_flag_set(int remove_flag)
{
	s_smart_card_remove_flag = remove_flag;
	DEBUG("s_smart_card_insert_flag=%d, s_smart_card_remove_flag=%d\n", s_smart_card_insert_flag,s_smart_card_remove_flag);
	
	return s_smart_card_remove_flag;
}

int smart_card_remove_flag_get()
{
	return s_smart_card_remove_flag;
}


/*
 从智能卡中查询指定的产品信息。
*/
static int check_productid_from_smartcard(char *productid)
{
	if(NULL==productid){
		DEBUG("invalid arg\n");
		return -1;
	}
	
	if(0==s_smart_card_insert_flag){
		DEBUG("no smart card has inserted\n");
		return -1;
	}
	
/*
 查询授权信息
*/
	CDCA_U32 dwFrom = 0, dwNum = 128;
	unsigned int i = 0;
	SCDCAPVODEntitleInfo EntitleInfo[128];

	int ret = CDCASTB_DRM_GetEntitleInfo(&dwFrom,EntitleInfo,&dwNum);
	if(CDCA_RC_OK==ret){
		DEBUG("dwFrom=%lu, dwNum=%lu\n", dwFrom, dwNum);
		long check_productid = strtol(productid,NULL,0);
		for(i=0;i<dwNum;i++){
			if(((unsigned long)check_productid)==EntitleInfo[i].m_ID){
				DEBUG("check %s ok, mirror with %lu\n", productid, EntitleInfo[i].m_ID);
				return 0;
			}
			else
				DEBUG("EntitleInfo[%d].m_ID=%lu\n", i, EntitleInfo[i].m_ID);
		}
		
		return -1;
	}
	else{
		drm_errors("CDCASTB_DRM_GetEntitleInfo", ret);
		return -1;
	}
}



/*
 检查指定的产品id是否在特殊产品之列。
*/
int special_productid_check(char *productid)
{
	if(NULL==productid)
		return -1;
	
	if(0==check_productid_from_smartcard(productid)){
		DEBUG("check %s from smartcard entitle OK\n", productid);
		return 0;
	}
	
	DEBUG("productid=%s, s_TestSpecialProductID=%s\n", productid,s_TestSpecialProductID);
	
	if(0==strcmp(productid, s_TestSpecialProductID)){
		DEBUG("check productid ok with s_TestSpecialProductID\n");
		return 0;
	}
	else
		return -1;
}

int setting_init_with_database()
{
	cur_language_init();
	serviceID_init();
	push_dir_init();
	guidelist_select_refresh();
	SCEntitleInfo_init();
	
	TestSpecialProductID_init();
	
	return 0;
}

