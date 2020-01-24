/* drivers/input/cs_press/cs_press_f61.c
 *
 * 2017 - 2020 Chipsea Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the CHIPSEA's PRESS IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Revision Record:
 *      V1.0:
 *          first release. By Sanny, 2017/07/26.
 *      V1.2:
 *          add hex_fw_update. By Sanny, 2017/08/22.
 *      V1.3:
 *          conform to the standard. By Sanny, 2019/01/16.
 *
 */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>

#include <linux/netdevice.h>
#include <linux/mount.h>
#include <linux/proc_fs.h>

#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/completion.h>
#include <linux/kthread.h>

#include <linux/regulator/of_regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/pinctrl/consumer.h>


#ifdef CONFIG_MTK_I2C_EXTENSION
#include <linux/dma-mapping.h>
#endif

/******* Macro definition **********/
#define LOG(fmt, args...) printk(KERN_DEBUG "[cs_press] [%s: %d] "fmt,  __func__, __LINE__, ##args)

#define FW_PATH		"/system/etc/firmware/pressure_f61.nfw"
#define FW_PATH_1	"/data/cs_press/pressure_f61.nfw"

#define READ_FW_ROM_TO_FILE "/sdcard/cs_press_fw.txt"

#define CS_CHRDEV_NAME "cs_press"
#define CS_I2C_ADDR		0x50 /* (0xA0>>1) */

/*IIC REG*/
#define IIC_EEPROM       0x00
#define IIC_DEV_ID       0x02
#define IIC_MANU_ID      0x03
#define IIC_MODULE_ID    0x04
#define IIC_FW_VER       0x05
#define IIC_WAKE_UP      0x06
#define IIC_SLEEP        0x07
#define IIC_GREEN        0x08
#define IIC_GREEN2NORMAL 0x10
#define IIC_HOSTSTATUS   0x50
#define IIC_DEBUG_MODE   0x60
#define IIC_DATA_READY   0x61
#define IIC_DEBUG_DATA1  0x62
#define IIC_DEBUG_DATA2  0x63
#define IIC_DEBUG_DATA3  0x64
#define IIC_DEBUG_DATA4  0x65

#define IIC_DEBUG_MODE2   0x80
#define IIC_DEBUG_READY2   0x81
#define IIC_DEBUG2_DATA  0x82

#define IIC_KEY_SEN      0xD0
#define IIC_KEY_EVENT      0xD3

#define IIC_LEFT_KEY_PRESS 0x21
#define IIC_RIGHT_KEY_PRESS 0x22

#define IIC_KEY_STAT			0x20
#define CS_PRESS_KEY_IDLE		0x00
#define CS_PRESS_KEY_DOWN		0x01
#define CS_PRESS_KEY_MOVE		0x02
#define CS_PRESS_KEY_UP  		0x03


#define CS_DEVID_LEN    0xA /* device id length */
#define CS_MANUID_LEN   0x2 /* manufacture id length */
#define CS_MODULEID_LEN 0x2 /* module id length */
#define CS_FWID_LEN     0x2 /* firmware image length */

#define DEBUG_RAW_MODE	0x10
#define DEBUG_DIFF_MODE	0x20

#define CURRENT_LOAD_UA 	(200000)//200mA

#define IIC_RESETCMD 0xf17c
#define FW_UPDATA_MAX_LEN (64*1024)

#define CHECK_FW_DELAY_TIME  (2000)
#define CHECK_DELAY_TIME	(20000)
#define I2C_CHECK_SCHEDULE
#define INT_SET_EN

//#define HOME_KEY
#define SIDE_KEY
#define KEY_LEFTHOLDDOWN    1000
#define KEY_RIGHTHOLDDOWN   1001

#define PRESS_THD_MIN		100
#define PRESS_THD_MAX		30000


enum {
	POWER_OFF = 0,
	POWER_ON,
	POWER_PROTECT,
	POWER_I2C_CHECK,
	POWER_SUSPEND,
};

/******* value definition **********/
unsigned short g_cs_reg;
static struct i2c_client *g_cs_client;
struct pinctrl *cs_press_pinctrl=NULL;
struct pinctrl_state *cs_press_irq_stat=NULL;
struct pinctrl_state *cs_press_rst1;
static struct regulator * power_3v3;

static struct mutex	i2c_rw_lock;
atomic_t power_stat = ATOMIC_INIT(0);
atomic_t sys_stat = ATOMIC_INIT(0);

static DEFINE_MUTEX(i2c_rw_lock);

int defail_reset_mode = 1;

/*values for read/write any register.*/
unsigned char read_reg_data[64];
int read_reg_len;

#ifdef I2C_CHECK_SCHEDULE
struct delayed_work i2c_check_worker;
#endif
struct delayed_work update_worker;

static int press_threshold[2] = { 60, 50, };
static int debug_mode;
static unsigned int cali_param[1] = {100};
static int cali_channel;


#ifdef INT_SET_EN
static DECLARE_WAIT_QUEUE_HEAD(cs_press_waiter);
static int cs_press_int_flag;
int cs_press_irq;
int cs_press_rst;
/*1 enable,0 disable,  need to confirm after register eint*/
static int cs_irq_flag = 1;
struct input_dev *cs_input_dev;
#endif

/******* fuction definition start **********/
#ifdef INT_SET_EN
static void cs_irq_enable(void);
static void cs_irq_disable(void);
#endif

static int cs_open(struct inode *inode, struct file *file);
static int cs_close(struct inode *inode, struct file *file);
static loff_t cs_lseek(struct file *file, loff_t offset, int whence);
static ssize_t cs_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset);
static ssize_t cs_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset);
void cs_rst_set(void);
void cs_set_rst_pin(int val);
/******* fuction definition end **********/

static int cs_i2c_read(unsigned char reg, unsigned char *datbuf, int byte_len)
{
	struct i2c_msg msg[2];
	int ret = 0;
	int i = 3;

	msg[0].addr  = g_cs_client->addr;
	msg[0].flags = 0;
	msg[0].len   = 1;
	msg[0].buf   = &reg;

	msg[1].addr  = g_cs_client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = byte_len;
	msg[1].buf   = datbuf;

	mutex_lock(&i2c_rw_lock);
	do {
		ret = i2c_transfer(g_cs_client->adapter, msg,
			sizeof(msg) / sizeof(struct i2c_msg));
		if (ret <= 0)
		{
			if(i <= 1){
				LOG("i2c_transfer Error ! err_code:%d,try_times=%d\n", ret,4-i);
				break;
			}
		}else{
			break;
		}
		msleep(5);
		i--;
	} while (i > 0);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int cs_i2c_write(unsigned char reg,
		unsigned char *datbuf, int byte_len)
{
	unsigned char *buf;
	struct i2c_msg msg;
	int ret = 0;
	int i = 5;

	if (!datbuf || byte_len <= 0)
		return -1;

	buf = (unsigned char *)kmalloc(byte_len + 1, GFP_KERNEL);
	if (!buf)
		return -1;

	memset(buf, 0, byte_len + 1);
	buf[0] = reg;
	memcpy(buf + 1, datbuf, byte_len);

	msg.addr  = g_cs_client->addr;
	msg.flags = 0;
	msg.len   = byte_len + 1;
	msg.buf   = buf;

	mutex_lock(&i2c_rw_lock);
	do {
		ret = i2c_transfer(g_cs_client->adapter, &msg, 1);
		if (ret <= 0)
			LOG("i2c_transfer Error! err_code:%d,i=%d\n", ret, i);
		else
			break;

		i--;
	} while ( i> 0);
	mutex_unlock(&i2c_rw_lock);

	kfree(buf);

	return ret;
}
static int cs_i2c_double_read(unsigned short reg,
	unsigned char *datbuf, int byte_len)
{
	struct i2c_msg msg[2];
	int ret = 0;
	unsigned char reg16[2];


	if (!datbuf)
		return -1;

	reg16[0] = (reg >> 8) & 0xff;
	reg16[1] = reg & 0xff;

	msg[0].addr  = g_cs_client->addr;
	msg[0].flags = 0;
	msg[0].len   = sizeof(reg16);
	msg[0].buf   = reg16;

	msg[1].addr  = g_cs_client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = byte_len;
	msg[1].buf   = datbuf;

	ret = i2c_transfer(g_cs_client->adapter, msg,
		sizeof(msg) / sizeof(struct i2c_msg));
	if (ret < 0)
		LOG("i2c_transfer Error ! err_code:%d\n", ret);

	return ret;
}

static int cs_i2c_double_write(unsigned short reg,
	unsigned char *datbuf, int byte_len)
{
	unsigned char *buf = NULL;
	struct i2c_msg msg;
	int ret = 0;

	if (!datbuf || byte_len <= 0)
		return -1;
	buf = (unsigned char *)kmalloc(byte_len + sizeof(reg), GFP_KERNEL);
	if (!buf)
		return -1;

	memset(buf, 0, byte_len + sizeof(reg));
	buf[0] = (reg >> 8) & 0xff;
	buf[1] = reg & 0xff;
	memcpy(buf + sizeof(reg), datbuf, byte_len);

	msg.addr  = g_cs_client->addr;
	msg.flags = 0;
	msg.len   = byte_len + sizeof(reg);
	msg.buf   = buf;

	ret = i2c_transfer(g_cs_client->adapter, &msg, 1);
	if (ret < 0)
		LOG("i2c_master_send Error ! err_code:%d\n", ret);

	kfree(buf);
	return ret;
}


static int cs_upgrade_erase(struct i2c_client *client)
{
	unsigned char erase_cmd[] = {0xAA, 0x55, 0xA5, 0x5A};

	/*erase flash*/
	if (cs_i2c_double_write(IIC_EEPROM, erase_cmd, sizeof(erase_cmd)) <= 0) {
		LOG("cs_i2c_double_write fails in cs_upgrade_erase\n");
		return -1;
	}

	msleep(2000);
	return 0;
}

int cs_upgrade_skip(struct i2c_client *client)
{
	unsigned char erase_cmd[] = {0x7E, 0xE7, 0xEE, 0x77};

	if (cs_i2c_double_write(IIC_EEPROM, erase_cmd, sizeof(erase_cmd)) <= 0) {
		LOG("cs_i2c_double_write fails in cs_upgrade_skip\n");
		return -1;
	}
	return 0;
}

static int cs_upgrade_read(void)
{
	unsigned char cmd[] = {0xA7, 0x00, 0x00, 0x59};

	if (cs_i2c_double_write(IIC_RESETCMD, cmd, sizeof(cmd)) <= 0) {
		LOG("cs_i2c_double_write fail !\n");
		return -1;
	}
	return 0;
}

/* shval=0,sorf reset; shval=1,hard reset; shval=2, defail reset.*/
static int cs_reset(int shval)
{
	unsigned char erase_cmd[] = {0xA0, 0x00, 0x00, 0x60};

	if (shval == 0)
		cs_i2c_double_write(IIC_RESETCMD, erase_cmd, sizeof(erase_cmd));
	else if (shval == 1)
		cs_rst_set();
	else
		if (defail_reset_mode == 0)
			cs_i2c_double_write(IIC_RESETCMD, erase_cmd, sizeof(erase_cmd));
		else if (defail_reset_mode == 1)
			cs_rst_set();


	return 0;
}
/*
* power_mode_switch()
* input value:
* mode = 0 : normal mode.
* mode = 1 : standby mode.
*/
/*static int power_mode_switch(int mode)
{

	unsigned char addr = 0;
	unsigned char data[1] = {0};
	int ret = 0;

	if (0 == mode) {
		data[0] = 0x0;
		addr = IIC_HOSTSTATUS;
		ret = cs_i2c_write(addr, data, sizeof(data));
		if (ret <= 0)
			LOG("CS:::switch mode err\n");
		ret = cs_i2c_write(addr, data, sizeof(data));
		if (ret <= 0)
			LOG("CS:::switch mode err\n");
	} else if (1 == mode) {
		data[0] = 0x1;
		addr = IIC_HOSTSTATUS;
		ret = cs_i2c_write(addr, data, sizeof(data));
		if (ret <= 0)
			LOG("CS:::switch mode err\n");
	}

	return ret;
}*/

int burn_fw(unsigned char *buf, int len)
{
	unsigned short reg = 0;
	int byte_len = 0;
	int pos = 0;
	unsigned char *read_buf;
	int ret = 0;
	int number = 0;
	bool i2c_ok_flag = true;
	int page_end = 0;
	char err_buf[256*3+3];
	int err_line = 16;
	int err_len = 0;
	int i;

	if (len % 128) {
		LOG("burn len is not 128*");
		return -1;
	}
	page_end = len%256;
	LOG("read page_end:%d\n", page_end);
	read_buf = (unsigned char *)kmalloc(len, GFP_KERNEL);
	if (NULL == read_buf) {
		LOG("kmalloc fails. line:%d\n", __LINE__);
		return -1;
	}
#ifdef INT_SET_EN
	cs_irq_disable(); /*close enit irq.*/
#endif
	mutex_lock(&i2c_rw_lock);

	do {
		cs_reset(2);
		msleep(60);

		ret = cs_upgrade_erase(g_cs_client);
		if (ret < 0) {
			i2c_ok_flag = false;
			goto I2C_BAD_OUT;
		}
		ret = 0;

		/*write upgrade*/
		reg = 0x00;
		pos = 0;
		byte_len = 128;
		while (pos < len) {
			ret = cs_i2c_double_write(pos, buf + pos, byte_len);
			if (ret < 0) {
				LOG("CS:::[%d] cs_i2c_double_write fails\n",
					__LINE__);
				i2c_ok_flag = false;
				goto I2C_BAD_OUT;
			}
			pos += byte_len;
			reg++;
			msleep(15);
		}

		/*read upgrade*/
		reg = 0x00;
		pos = 0;
		byte_len = 256;
		while (pos < len) {
			ret = cs_i2c_double_read(pos, read_buf + pos, byte_len);
			if (ret < 0) {
				LOG("CS:::[%d]read page fail !page:%d\n",
					__LINE__, reg);
				i2c_ok_flag = false;
				ret = -1;

				err_len = 0;
				for (i = 0; i < byte_len; i++){
					
					err_len += sprintf(err_buf + err_len,
						"%02x ", read_buf[pos + i]);
					if( i%err_line == 0)
						err_len += sprintf(err_buf + err_len, "\n");
				}
				err_len += sprintf(err_buf + err_len, "\n");
				LOG("buf=%s\n", err_buf);

				goto I2C_BAD_OUT;
			}
			/*check*/
			if ((page_end > 0) && (reg >= len/256))
				byte_len = page_end;
			if (memcmp(buf + pos, read_buf + pos, byte_len)) {
				LOG("CS:::[%d]read page cmp fail !page:%d\n",
					__LINE__, reg);
				i2c_ok_flag = false;
				ret = -1;

				err_len = 0;
				for (i = 0; i < byte_len; i++){
					err_len += sprintf(err_buf + err_len,
						"%02x ", read_buf[pos + i]);
					if( i%err_line == 0)
						err_len += sprintf(err_buf + err_len, "\n");
				}
				err_len += sprintf(err_buf + err_len, "\n");
				LOG("buf=%s\n", err_buf);

				goto I2C_BAD_OUT;
			}
			pos += byte_len;
			reg++;
			msleep(15);
		}
I2C_BAD_OUT:
		number++;
	} while ( number < 3 && ret<0 && i2c_ok_flag == false);


	if (ret<0 || i2c_ok_flag == false)
		LOG("[%d] burn firmware err\n", __LINE__);
	else
		LOG("[%d] burn firmware success\n", __LINE__);

	msleep(100);
	cs_reset(2);
	msleep(100);
	cs_upgrade_skip(g_cs_client);
	if (read_buf != NULL)
		kfree(read_buf);

	mutex_unlock(&i2c_rw_lock);
#ifdef INT_SET_EN
	cs_irq_enable(); /*open enit irq.*/
#endif
	return ret;
}


int cs_fw_update(void)
{
	struct file *fp;
	mm_segment_t fs;
	loff_t pos;
	int len = 0;
	char *fw_data;
	char invalid_flg = 0;
	unsigned char ic_fw_ver[CS_FWID_LEN] = {0};
	unsigned char file_fw_ver[CS_FWID_LEN] = {0};
	int ret = 1;

	ret = cs_i2c_read(IIC_FW_VER, ic_fw_ver, sizeof(ic_fw_ver));
	if (ret < 0) {
		LOG("fail to read firmware version\n");
		invalid_flg = 1;
	}

	fp = filp_open(FW_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		fp = filp_open(FW_PATH_1, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			LOG("open file error\n");
			return -1;
		}
	}
	fs = get_fs();
	set_fs(KERNEL_DS);

	pos = 8;
	vfs_read(fp, file_fw_ver, sizeof(file_fw_ver), &pos);
	*(unsigned short *)file_fw_ver = swab16(*(unsigned short *)file_fw_ver);

	if(invalid_flg == 0)
	{
		if (*(unsigned short *)ic_fw_ver >= *(unsigned short *)file_fw_ver) {
			LOG("[current fw:%x][previous:%x]\n",
				*(unsigned short *)ic_fw_ver,
				*(unsigned short *)file_fw_ver);
			goto close_file_out;
		}
		LOG("[current fw:%x][file fw:%x]\n",
			*(unsigned short *)ic_fw_ver, *(unsigned short *)file_fw_ver);
	}else
	{
		LOG("current version read failed! start to force update firmware version=%x\n", \
			*(unsigned short *)file_fw_ver);
	}
	pos = 0x0c;
	vfs_read(fp, (char *)&len, sizeof(len), &pos);
	len = swab32(len);
	LOG("[file fw len:%x]\n", len);
	if(len <= 0 || len > FW_UPDATA_MAX_LEN){
		LOG("[err!len overflow!len=%x]\n", len);
		goto close_file_out;
	}
		
	fw_data = (char *)kmalloc(len, GFP_KERNEL);
	if (!fw_data) {
		LOG("fail to malloc buffer\n");
		goto close_file_out;
	}

	pos = 0x100;
	vfs_read(fp, fw_data, len, &pos);

	ret = burn_fw(fw_data, len);
	if (ret == 0)
		LOG("Burning firmware fails\n");

	kfree(fw_data);
close_file_out:
	filp_close(fp, NULL);
	set_fs(fs);
	return ret;
}

int wake_up_fw(void)
{
	int retry_count = 3;
	int ret = 0;
	char reg_data[2];
	do {
		ret = cs_i2c_read(0x03, reg_data, 1);
		retry_count--;
		//msleep(10);
		if(retry_count == 0)
			LOG("wake up retry %d.\n", retry_count);
	} while(ret <= 0 && retry_count > 0);
	
	return ret;
}


int set_press_threshold(int touchTh, int leaveTh, char channel)
{
	unsigned char datbuf[8];

	LOG("w TouchTh:%d,LeaveTh:%d,channel:%d\n",
		touchTh, leaveTh, channel);

	wake_up_fw();
	datbuf[0] = 0x00;
	cs_i2c_write(0x80, datbuf, 1);

	datbuf[0] = 0x32;
	cs_i2c_write(0x80, datbuf, 1);

	datbuf[0] = 0x00;
	datbuf[1] = channel;

	datbuf[2] = touchTh & 0xff;
	datbuf[3] = (touchTh >> 8) & 0xff;
	datbuf[4] = leaveTh & 0xff;
	datbuf[5] = (leaveTh >> 8) & 0xff;

	cs_i2c_write(0x82, datbuf, 6);

	datbuf[0] = 0x06;
	
	cs_i2c_write(0x81, datbuf, 1);

	return 1;
}

int get_press_threshold(unsigned int *touchTh,
	unsigned int *leaveTh, char channel)
{
	unsigned char datbuf[8];
	unsigned int mTouchTh = 0;
	unsigned int mLeaveTh = 0;

	wake_up_fw();
	datbuf[0] = 0x00;
	cs_i2c_write(0x80, datbuf, 1);

	datbuf[0] = 0x33;
	cs_i2c_write(0x80, datbuf, 1);

	datbuf[0] = 0x00;
	datbuf[1] = channel;

	cs_i2c_write(0x82, datbuf, 2);

	datbuf[0] = 0x02;
	cs_i2c_write(0x81, datbuf, 1);
	msleep(30);

	datbuf[0] = 0;
	cs_i2c_read(0x81, datbuf, 1);
	if (datbuf[0] == 4) {
		memset(datbuf, 0, 8*sizeof(unsigned char));
		cs_i2c_read(0x82, datbuf, 4);
		mTouchTh = (unsigned int)(short)(datbuf[0]
			| ((datbuf[1]<<8)&0xff00));
		mLeaveTh = (unsigned int)(short)(datbuf[2]
			| ((datbuf[3]<<8)&0xff00));
		*touchTh = mTouchTh;
		*leaveTh = mLeaveTh;
		LOG("TouchTh:%d,LeaveTh:%d\n", mTouchTh, mLeaveTh);
		return 1;
	}
	return 0;
}

int read_eprom_data( char *buf)
{
	unsigned char datbuf[33];
	int i;
	int len = 33;
	int ret = 1;

	wake_up_fw();
	
	datbuf[0] = 0x00;
	cs_i2c_write(0x80, datbuf, 1);

	datbuf[0] = 0x40;
	cs_i2c_write(0x80, datbuf, 1);
	msleep(30);

	memset(datbuf, 0, len * sizeof(unsigned char));
	cs_i2c_read(0x81, datbuf, len);
	LOG("R 81:%02x,%02x \n", datbuf[0], datbuf[1]);
	if (datbuf[0] == len-1) {
		LOG("Read eprom 32 Data:\n");
		for (i = 0; i < datbuf[0]; i++)
			LOG("%d %02x\n", i, datbuf[i]);
	}

	for (i = 0; i < len; i++)
		ret += sprintf(buf + 3 * i, "%02x ", datbuf[i]);

	ret += sprintf(buf + 3 * i, "\n");

	return ret;
}


int write_calibrate_param(unsigned int data1, char channel)
{
	unsigned char datbuf[8];

	LOG("w calibrate_param:%d,channel:%d\n", data1, channel);

	wake_up_fw();

	datbuf[0] = 0x00;
	cs_i2c_write(IIC_DEBUG_MODE2, datbuf, 1);

	datbuf[0] = 0x30;
	cs_i2c_write(IIC_DEBUG_MODE2, datbuf, 1);

	datbuf[0] = channel;

	datbuf[1] = 0x00;

	datbuf[2] = data1 & 0xff;
	datbuf[3] = (data1 >> 8) & 0xff;
	datbuf[4] = (data1 >> 16) & 0xff;
	datbuf[5] = (data1 >> 24) & 0xff;
	datbuf[6] = datbuf[0] + datbuf[1] + datbuf[2]
		+ datbuf[3] + datbuf[4] + datbuf[5]; /*checksum*/
	cs_i2c_write(IIC_DEBUG2_DATA, datbuf, 6);

	datbuf[0] = 0x06;
	cs_i2c_write(IIC_DEBUG_READY2, datbuf, 1);

	return 1;

}

int read_calibrate_param(unsigned int *data1, char channel)
{
	unsigned char datbuf[8];
	unsigned int m_data = 0;

	wake_up_fw();

	datbuf[0] = 0x00;
	cs_i2c_write(IIC_DEBUG_MODE2, datbuf, 1);

	datbuf[0] = 0x31;
	cs_i2c_write(IIC_DEBUG_MODE2, datbuf, 1);

	datbuf[0] = channel;

	datbuf[1] = 0x00;
	cs_i2c_write(IIC_DEBUG2_DATA, datbuf, 2);

	datbuf[0] = 0x02;
	cs_i2c_write(IIC_DEBUG_READY2, datbuf, 1);
	msleep(30);

	memset(datbuf, 0, 8 * sizeof(unsigned char));
	cs_i2c_read(IIC_DEBUG_READY2, datbuf, 1);
	LOG("len:%02x\n", datbuf[0]);
	if (datbuf[0] == 4) {
		cs_i2c_read(IIC_DEBUG2_DATA, datbuf, 4);
		LOG("R:%02x,%02x,%02x,%02x\n", datbuf[0],
			datbuf[1], datbuf[2], datbuf[3]);
		m_data = (unsigned int)(datbuf[0] + (datbuf[1] << 8)
			+ (datbuf[2]<<16) + (datbuf[3] << 24));
		*data1 = m_data;
		LOG("R calibrate_param:%d\n", m_data);
		return 1;
	}
	return 0;
}

static int cs_press_set_power_mode(char power_mode)
{
	unsigned char rbuf[1];

	if(power_mode == POWER_OFF)
		cs_i2c_read(IIC_SLEEP, rbuf, 1);
	else if(power_mode == POWER_ON)
		cs_i2c_read(IIC_WAKE_UP, rbuf, 1);

	return 0;
}
/* check i2c err,for reset IC. */
int check_i2c(void)
{
	int retry = 3;
	unsigned char rbuf[2];
	unsigned char addr;
	int len = 0;

	addr = 0x03;
	len = 1;
	rbuf[0] = 0;
	rbuf[1] = 0;

	do {
		if (cs_i2c_read(addr, rbuf, len) >= 0)
			return 1;
		retry--;
		LOG("read fw ver fail!retry:%d\n", retry);
	} while (retry > 0);

	retry = 3;
	do {
		cs_reset(2);
		msleep(300);
		if (cs_i2c_read(addr, rbuf, len) >= 0)
			return 1;
		retry--;
		LOG("reset fw fail!retry:%d\n", retry);
	} while (retry > 0);

	return -1;
}
/* check fw err fo update fw, just use in start up. */
int check_fw_err(void)
{
	int retry = 3;
	int result = 0;

	do {
		if (check_i2c() < 0) {
			result = cs_fw_update();
			if (result < 1)
				LOG("update fw fail!retry:%d\n", retry);
		} else {
			return 1;
		}
	retry--;
	} while (retry > 0);
	return 0;
}

#ifdef I2C_CHECK_SCHEDULE
static void  i2c_check_func(struct work_struct *worker)
{
	int ret = 0;

	if((atomic_read(&power_stat)!=POWER_ON) ||
		(atomic_read(&sys_stat)==POWER_SUSPEND))
	{
		LOG("i2c_check_func cancel because the system goint to sleep.power_stat:%d sys_stat:%d\n", 
			CHECK_DELAY_TIME,
			atomic_read(&power_stat),
			atomic_read(&sys_stat));
		schedule_delayed_work(&i2c_check_worker, msecs_to_jiffies(CHECK_DELAY_TIME));
		LOG("i2c_check_func start,delay %d ms.\n", CHECK_DELAY_TIME);
		return;
	}

	atomic_set(&power_stat, POWER_I2C_CHECK);
	ret = check_i2c();
	atomic_set(&power_stat, POWER_ON);

	schedule_delayed_work(&i2c_check_worker, msecs_to_jiffies(CHECK_DELAY_TIME));
	LOG("i2c_check_func start,delay %d ms.\n", CHECK_DELAY_TIME);

}
#endif

static ssize_t cs_switch_irq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
#ifdef INT_SET_EN
	ret =  sprintf(buf, "%d\n",cs_irq_flag);
	LOG("irq flag=%d\n",cs_irq_flag );
#endif
	return ret;
}
static ssize_t cs_switch_irq_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
#ifdef INT_SET_EN
	if (buf[0] == '0'){
		cs_irq_disable();
		LOG("disable irq\n");
	} else if (buf[0] == '1'){
		cs_irq_enable();
		LOG("enable irq\n");
	}
#endif
	return 1;
}

int force_update_fw(void)
{
	struct file *fp = NULL;
	mm_segment_t fs;
	loff_t pos = 0;
	unsigned int len = 0;
	char *fw_data = NULL;
	ssize_t ret = 0;

	fp = filp_open(FW_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		fp = filp_open(FW_PATH_1, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			LOG("open file error\n");
		return -1;
		}
	}
	fs = get_fs();
	set_fs(KERNEL_DS);

	pos = 0x0c;
	vfs_read(fp, (char *)&len, 4, &pos);
	len = swab32(len);
	LOG("fw len = %d\n", len);
	if(len <= 0 || len > FW_UPDATA_MAX_LEN){
		LOG("[err! len overflow!len=%x]\n", len);
		goto exit_fw_buf;
	}
	
	fw_data = (char *)kmalloc(len, GFP_KERNEL);
	if (!fw_data) {
		LOG("fail to kmalloc buffer\n");
		ret = -ENOMEM;
		goto exit_fw_buf;
	}
	pos = 0x100;
	vfs_read(fp, fw_data, len, &pos);

	ret = burn_fw(fw_data, len);
	if (ret <= 0)
		LOG("Burning firmware fails\n");

	kfree(fw_data);
exit_fw_buf:
	set_fs(fs);
	filp_close(fp, NULL);
	return ret;
}

static ssize_t cs_force_update_fw_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return force_update_fw();

}

static ssize_t cs_force_update_fw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	int result = 0;

	result = force_update_fw();
	if (result >= 1)
		ret = sprintf(buf, "%d,pass!\n", result);
	else
		ret = sprintf(buf, "%d,failed!\n", result);

	LOG("%s\n", buf);
	return ret+1;
}

static ssize_t cs_fw_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i   = 0;
	int ret = 0;
	unsigned char dev_id[CS_DEVID_LEN]       = {0};
	unsigned char manu_id[CS_MANUID_LEN]     = {0};
	unsigned char module_id[CS_MODULEID_LEN] = {0};
	unsigned char fw_ver[CS_FWID_LEN]        = {0};

	wake_up_fw();;

	ret = cs_i2c_read(IIC_DEV_ID, dev_id, sizeof(dev_id));
	if (ret < 0)
		LOG("fail to read device id\n");
	ret = cs_i2c_read(IIC_MANU_ID, manu_id, sizeof(manu_id));
	if (ret < 0)
		LOG("fail to read manufacture id\n");
	ret = cs_i2c_read(IIC_MODULE_ID, module_id, sizeof(module_id));
	if (ret < 0)
		LOG("fail to read module id\n");
	ret = cs_i2c_read(IIC_FW_VER, fw_ver, sizeof(fw_ver));
	if (ret < 0)
		LOG("fail to read firmware version\n");

	ret = sprintf(buf, "device id:");
	buf += ret;
	for (i = 0; i < sizeof(dev_id); i++)
		ret += sprintf(buf + 2 * i, "%02x", dev_id[i]);

	ret += sprintf(buf + 2 * sizeof(dev_id),
	"\nmanu id:%02x%02x\nmodule id:%02x%02x\nfw version:%02x%02x\n",
		manu_id[0], manu_id[1], module_id[0],
		module_id[1], fw_ver[1], fw_ver[0]);

	return ret + 1;
}

int set_debug_mode(unsigned char addr,unsigned char data){
	int ret;
	unsigned char reg_addr;
	unsigned char reg_data[2];
	int len = 1;
	
	reg_addr = addr;
	len = 1;
	reg_data[0] = data;
	ret = cs_i2c_write(reg_addr, reg_data, len);
	if (ret <= 0) {
		LOG("reg=%d, data=%d, len=%d, err\n",
			reg_addr, reg_data[0], len);
	}
	reg_addr = IIC_DEBUG_READY2;
	len = 1;
	reg_data[0] = 0x0;
	ret = cs_i2c_write(reg_addr, reg_data, len);
	if (ret <= 0) {
		LOG("reg=%d, data=%d, len=%d, err\n",
			reg_addr, reg_data[0], len);
	}
	return ret;
}

int get_debug_data_ready(unsigned char addr){
	int ret;
	unsigned char reg_addr;
	unsigned char reg_data[1];
	int len = 1;
	
	reg_addr = addr;
	len = 1;
	reg_data[0] = 0;
	ret = cs_i2c_read(reg_addr, reg_data, len);
	if (ret <= 0) {
		LOG("reg=%d, data=%d, len=%d, err\n",
			reg_addr, reg_data[0], len);
	}
	return reg_data[0];
}

int get_debug_data(unsigned char addr,unsigned char* data, int len){
	int ret;
	unsigned char reg_addr;
	unsigned char reg_data[len];
	int i;

	reg_addr = addr;

	reg_data[0] = 0;
	ret = cs_i2c_read(reg_addr, reg_data, len);
	if (ret <= 0) 
	{
		LOG("reg=%d, data=%d, len=%d, err\n",
			reg_addr, reg_data[0], len);
	}
	for(i=0;i<len;i++)
		data[i] = reg_data[i];

	return ret;
}

static ssize_t cs_get_raw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned char reg_addr;
	unsigned char reg_data[64];

	int len = 1;
	int raw_data[64];
	int i;

	LOG("buf = %s \n", buf);

	wake_up_fw();

	set_debug_mode(IIC_DEBUG_MODE2, DEBUG_RAW_MODE);
	i=50;
	do {
		len = get_debug_data_ready(IIC_DEBUG_READY2);
		LOG("len:%d\n", len);
		if (len > 0) {
			ret = get_debug_data(IIC_DEBUG2_DATA, reg_data, len);
			LOG("D0:%d,D1:%d,D2:%d,len:%d\n", reg_data[0], reg_data[1], reg_data[2], len);
		}
		i--;
	} while(len == 0 && i > 0);

	ret = 0;
if(len > 0){
		if(len > 40)
			len = 40;
		for (i = 0; i < len/2; i++) {
			raw_data[i] = ((int)(reg_data[i*2] & 0xff)
				| ((int)(reg_data[i*2 + 1] & 0xff) << 8));

			ret += sprintf(buf + 6 * i, "%05d ", raw_data[i]);
			LOG("raw_data %d=%d\n", i, raw_data[i]);
		}

		ret += sprintf(buf + 6 * i, "\n");
		LOG("buf=%s\n", buf);
	}else{
		ret = sprintf(buf , "read ready len  zero!\n");
	}
	reg_addr = IIC_DEBUG_MODE2;
	len = 1;
	reg_data[0] = 0x0;
	cs_i2c_write(reg_addr, reg_data, len);
	
	return ret;
}
static ssize_t cs_get_diff_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned char reg_addr;
	unsigned char reg_data[64];

	int len = 1;
	int raw_data[64];
	int i;

	LOG("buf = %s \n", buf);

	wake_up_fw();
	
	set_debug_mode(IIC_DEBUG_MODE2, DEBUG_DIFF_MODE);
	i=50;
	do {
		len = get_debug_data_ready(IIC_DEBUG_READY2);
		LOG("len:%d\n", len);
		if (len > 0) {
			ret = get_debug_data(IIC_DEBUG2_DATA, reg_data, len);
			LOG("D0:%d,D1:%d,D2:%d,len:%d\n", reg_data[0], reg_data[1], reg_data[2], len);
		}
		i--;
	} while(len == 0 && i > 0);
	
	ret = 0;
	if(len > 0){
		
		for (i = 0; i < len/2; i++) {
			raw_data[i] = ((int)(reg_data[i*2] & 0xff)
				| ((int)(reg_data[i*2 + 1] & 0xff) << 8));

			ret += sprintf(buf + 6 * i, "%05d ", raw_data[i]);
			LOG("diff_data %d=%d\n", i, raw_data[i]);
		}

		ret += sprintf(buf + 6 * i, "\n");
		LOG("buf=%s\n", buf);
	}else{
		ret = sprintf(buf , "read ready len zero!\n");
	}
	reg_addr = 0x80;
	len = 1;
	reg_data[0] = 0x0;
	cs_i2c_write(reg_addr, reg_data, len);

	return ret;
}


static ssize_t cs_reset_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int val = 0;

	if(buf[0] == '0')
		val = 0;
	else if(buf[0] == '1')
		val = 1;
	LOG("val:%d,buf:%s\n", val,  buf);
	cs_set_rst_pin(val);
	return count;
}

static ssize_t cs_sorf_hard_reset_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int val = 1;

	if(buf[0] == '0')
		val = 0;
	else if(buf[0] == '1')
		val = 1;
	LOG("buf:%s,val:%d\n",  buf, val);
	defail_reset_mode = val;

	return count;
}

static ssize_t cs_rw_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	int i = 0;

	if (read_reg_len != 0) {
		for (i = 0; i < read_reg_len; i++)
			ret += sprintf(buf + 3 * i, "%02x ", read_reg_data[i]);

	}
	ret += sprintf(buf + 3 * i, "\n");

	LOG("buf=%s\n", buf);

	return ret;
}

/*write  register*/
static ssize_t cs_rw_reg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	const char *startpos = buf;
	const char *lastc = buf + count;
	unsigned int tempdata = 0;
	char *firstc = NULL;
	int idx = 0;
	int ret = 0;
	int set_mode = 0;
	unsigned char change_val[32] = { 0 };

	if (!buf || count <= 0) {
		LOG("CS::argument err\n");
		return -EINVAL;
	}

	while (startpos < lastc) {
		LOG("idx:%d\n", idx);
		firstc = strstr(startpos, "0x");
		if (!firstc) {
			LOG("CS::cant find firstc\n");
			return -EINVAL;
		}

		firstc[4] = 0;

		ret = kstrtouint(startpos, 0, &tempdata);
		if (ret) {
			LOG("CS::fail to covert digit\n");
			return -EINVAL;
		}
		if (idx == 0) {
			set_mode = tempdata;
			LOG("set_mode:%d\n", set_mode);
		} else {
			change_val[idx - 1] = tempdata;
			LOG("tempdata:%d\n", tempdata);
		}

		startpos = firstc + 5;

		idx++;

		if (set_mode == 0 && idx > 3 && idx >= change_val[1] + 3)
			break;
		else if (set_mode == 1 && idx > 3)
			break;

	}

	if (set_mode == 0) {
		cs_i2c_write(change_val[0],
			&change_val[2], (int)change_val[1]);
		read_reg_len = 0;
	} else if (set_mode == 1) {
		cs_i2c_read(change_val[0],
			&read_reg_data[0], (int)change_val[1]);
		read_reg_len = change_val[1];
	} else if (set_mode == 2) {
		cs_i2c_double_write(((change_val[0]<<8) | change_val[1]),
			&change_val[3], (int)change_val[2]);
		read_reg_len = 0;
	} else if (set_mode == 3) {
		cs_i2c_double_read(((change_val[0]<<8) | change_val[1]),
			&read_reg_data[0], (int)change_val[2]);
		read_reg_len = change_val[2];
	} else {
		read_reg_len = 0;
	}

	return count;
}

static ssize_t cs_read_fw_rom_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct file *fp = NULL;
	mm_segment_t fs;
	loff_t pos = 0;
	unsigned int len = 48 * 1024;
	ssize_t ret = 0;

	unsigned short reg = 0;
	int byte_len = 0;
	char *read_buf;
	char err_buf[256*3+3];
	unsigned int err_len = 0;
	int i;

	read_buf = (char *)kmalloc(len, GFP_KERNEL);
	if (NULL == read_buf) {
		LOG("kmalloc fails. line:%d\n", __LINE__);
		return -1;
	}
	memset(read_buf, 0, len);
#ifdef INT_SET_EN
	cs_irq_disable();
#endif
	mutex_lock(&i2c_rw_lock);
/*read upgrade*/
		cs_reset(2);
		msleep(60);

		cs_upgrade_read();

		reg = 0x00;
		pos = 0;
		byte_len = 256;
		while (pos < len) {
			ret = cs_i2c_double_read(pos, read_buf + pos, byte_len);
			if (ret < 0) {
				LOG("read page fail !page:%d\n", reg);
				ret = -1;

				err_len = 0;
				for (i = 0; i < byte_len; i++)
					err_len += sprintf(err_buf + err_len,
						"%02x ", read_buf[pos + i]);

				err_len += sprintf(err_buf + err_len, "\n");
				LOG("buf=%s\n", err_buf);
				goto READ_FW_ROM_ERR;
			}
			pos += byte_len;
			reg++;
			msleep(15);
		}

	cs_reset(2);
	msleep(100);
	cs_upgrade_skip(g_cs_client);
READ_FW_ROM_ERR:
	mutex_unlock(&i2c_rw_lock);
#ifdef INT_SET_EN
	cs_irq_enable();
#endif

	fp = filp_open(READ_FW_ROM_TO_FILE, O_RDWR, 0);
	if (IS_ERR(fp)) {
		LOG("open file error\n");
		return -1;
	}
	fs = get_fs();
	set_fs(KERNEL_DS);

	pos = 0x00;
	vfs_write(fp, read_buf, len, &pos);

	set_fs(fs);
	filp_close(fp, NULL);

	if (read_buf != NULL)
		kfree(read_buf);

	return ret;
}

static ssize_t cs_debug_th_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 1;

	int change_val[2]={100,0};
	
	LOG("debug_mode:%d \n", debug_mode);
	if (debug_mode == 0) {
		ret = read_eprom_data(buf);
		return ret;
	} else if (debug_mode == 2) {
		change_val[0] = press_threshold[0];
		change_val[1] = press_threshold[1];
	}

	LOG("%d,%d \n", change_val[0], change_val[1]);

	ret = sprintf(buf, "%d,%d\n", change_val[0], change_val[1]);
	return ret;

}

static ssize_t cs_debug_th_store(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
	const char *startpos = buf;
	const char *lastc = buf + count;
	unsigned int tempdata = 0;
	char *firstc = NULL;
	int idx = 0;
	int ret = 0;
	int change_val[3] = {60, 50, 0};
	int set_mode = 0;

	if (!buf || count <= 0) {
		LOG("CS::argument err\n");
		return -EINVAL;
	}

	while (startpos < lastc) {
		LOG("idx:%d\n", idx);
		firstc = strstr(startpos, "0x");
		if (!firstc) {
			LOG("CS::cant find firstc\n");
			return -EINVAL;
		}
		if ((idx == 0) | (idx >= 3))
			firstc[4] = 0;
		else if ((idx == 1) | (idx == 2))
			firstc[6] = 0;

		ret = kstrtouint(startpos, 0, &tempdata);
		if (ret) {
			LOG("CS::fail to covert digit\n");
			return -EINVAL;
		}
		if (idx == 0) {
			set_mode = tempdata;
			LOG("set_mode:%d\n", set_mode);
		} else if (idx > 0 && idx < 4) {
			change_val[idx - 1] = tempdata;
			LOG("tempdata:%d\n", tempdata);
		}
		if ((idx == 0) | (idx >= 3))
			startpos = firstc + 5;
		else if ((idx == 1) | (idx == 2))
			startpos = firstc + 7;

		idx++;

		if (idx >= 4)
			break;
	}

	if (set_mode == 1) {
		set_press_threshold(change_val[0],
			change_val[1], change_val[2]);
	} else if (set_mode == 2) {
		get_press_threshold(&press_threshold[0],
			&press_threshold[1], change_val[2]);
		debug_mode = 2;
	} else if (set_mode == 5) { 
		debug_mode = 0;
	}

	return count;
}

static ssize_t cs_cali_param_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = 0;

	LOG("debug_mode:%d,cali:%d \n", debug_mode, cali_param[0]);

	ret = sprintf(buf, "%d\n", cali_param[0]);

	return ret;
}
static ssize_t cs_cali_param_store(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
	const char *startpos = buf;
	const char *lastc = buf + count;
	unsigned int tempdata = 0;
	char *firstc = NULL;
	int idx = 0;
	int ret = 0;
	unsigned int change_val[2] = {100,  0};
	int set_mode = 0;

	if (!buf || count <= 0) {
		LOG("argument err\n");
		return -EINVAL;
	}

	while (startpos < lastc) {
		LOG("idx:%d\n", idx);
		firstc = strstr(startpos, "0x");
		if (!firstc) {
			LOG("cant find firstc\n");
			return -EINVAL;
		}
		if ((idx == 0) || (idx > 1))
			firstc[4] = 0;
		else if (idx == 1)
			firstc[10] = 0;

		ret = kstrtouint(startpos, 0, &tempdata);
		if (ret) {
			LOG("fail to covert digit\n");
			return -EINVAL;
		}
		if (idx == 0) {
			set_mode = tempdata;
			LOG("set_mode:%d\n", set_mode);
		} else if (idx >= 1 && idx < 4) {
			change_val[idx-1] = tempdata;
			LOG("val:%d\n", change_val[idx-1]);
		}

		if (idx == 0)
			startpos = firstc + 5;
		else if (idx == 1)
			startpos = firstc + 11;
		else if (idx > 1)
			startpos = firstc + 5;

		idx++;

		if (idx >= 3)
			break;
	}

	if (set_mode == 1) {
		write_calibrate_param(change_val[0], (char)change_val[1]);
	} else if (set_mode == 2) {
		read_calibrate_param(cali_param, (char)change_val[1]);
		cali_channel = change_val[1];
	}

	return count;
}
static ssize_t cs_power_stat_store(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
	int ret = 0;
	int cnt = 0;
	unsigned int swc_val = 0;

	if (!buf || count <= 0) {
		LOG("CS::argument err\n");
		return -EINVAL;
	}

	ret = kstrtouint(buf, 0, &swc_val);
	if (ret) {
		LOG("CS::fail to covert digit\n");
		return -EINVAL;
	}
	if(swc_val && atomic_read(&power_stat))
	{
		LOG("Current power is on, no need to re-power-on.\n", swc_val);
		return count;
	}
	if(!swc_val && !atomic_read(&power_stat))
	{
		LOG("Current power is down, no need to re-power-down.\n", swc_val);
		return count;
	}
	if(swc_val)
	{
		/*ret = regulator_enable(power_3v3);
		if(ret)
		{
			LOG("cs_press power on failed!\n");
			return ret;
		}*/
		cs_press_set_power_mode(POWER_ON);
		atomic_set(&power_stat, POWER_ON);
		LOG("Set cs_press power status to wakeup.\n");
	}else
	{
		if(atomic_read(&power_stat) == POWER_PROTECT)
		{
			LOG("The device is updating! forbid to power off!\n");
			return count;
		}
		while(atomic_read(&power_stat) == POWER_I2C_CHECK)
		{
			LOG("The device is i2c checking! power off later. cnt=%d!\n", cnt);
			msleep(500);
			if(cnt++ > 5)
				return count;
		}

		atomic_set(&power_stat, POWER_OFF);
		cs_press_set_power_mode(POWER_OFF);
		/*ret = regulator_disable(power_3v3);
		if(ret)
		{
			LOG("cs_press power down failed!\n");
			return ret;
		}*/
		LOG("Set cs_press power status to sleep.\n");
	}

	return count;
}
static ssize_t cs_power_stat_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = sprintf(buf, "%d\n", atomic_read(&power_stat));

	LOG("Get cs_press power status is %d.\n", atomic_read(&power_stat));

	return ret;
}
static ssize_t cs_press_thd_store(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
	int ret = 0;
	unsigned int thd_val = 0;

	if (!buf || count <= 0) {
		LOG("CS::argument err\n");
		return -EINVAL;
	}

	ret = kstrtouint(buf, 0, &thd_val);
	if (ret) {
		LOG("CS::fail to covert digit\n");
		return -EINVAL;
	}
	if(thd_val < PRESS_THD_MIN)
	{
		thd_val = PRESS_THD_MIN;
	}
	if(thd_val > PRESS_THD_MAX)
	{
		thd_val = PRESS_THD_MAX;
	}

	set_press_threshold(thd_val, thd_val*80/100, 0);

	return count;
}
static ssize_t cs_press_thd_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = 0;
	unsigned int thd_down_val = 0;
	unsigned int thd_up_val = 0;

	get_press_threshold(&thd_down_val, &thd_up_val, 0);

	ret = sprintf(buf, "%d\n", thd_down_val);

	return ret;
}

static DEVICE_ATTR(force_update_fw, S_IRUGO | S_IWUSR,
		cs_force_update_fw_show, cs_force_update_fw_store);
static DEVICE_ATTR(fw_info, S_IRUGO , cs_fw_info_show, NULL);
static DEVICE_ATTR(get_raw, S_IRUGO , cs_get_raw_show, NULL);
static DEVICE_ATTR(get_diff, S_IRUGO , cs_get_diff_show, NULL);
static DEVICE_ATTR(switch_irq, S_IRUGO | S_IWUSR, cs_switch_irq_show,
		cs_switch_irq_store);
static DEVICE_ATTR(reset,  S_IWUSR,
		NULL, cs_reset_store);
static DEVICE_ATTR(sorf_hard_reset,  S_IWUSR,
		NULL, cs_sorf_hard_reset_store);
static DEVICE_ATTR(rw_reg, S_IRUGO | S_IWUSR,
		cs_rw_reg_show, cs_rw_reg_store);
static DEVICE_ATTR(read_fw_rom, S_IRUGO , cs_read_fw_rom_show, NULL);
static DEVICE_ATTR(debug_th, S_IRUGO | S_IWUSR,
	cs_debug_th_show, cs_debug_th_store);
static DEVICE_ATTR(cali_param, S_IRUGO | S_IWUSR,
	cs_cali_param_show, cs_cali_param_store);
static DEVICE_ATTR(power_stat, S_IRUGO | S_IWUSR, cs_power_stat_show,
		cs_power_stat_store);
static DEVICE_ATTR(press_thd, S_IRUGO | S_IWUSR, cs_press_thd_show,
		cs_press_thd_store);


static struct attribute *cs_sys_attrs[] = {
	&dev_attr_force_update_fw.attr,
	&dev_attr_fw_info.attr,
	&dev_attr_get_raw.attr,
	&dev_attr_get_diff.attr,
	&dev_attr_switch_irq.attr,
	&dev_attr_reset.attr,
	&dev_attr_sorf_hard_reset.attr,
	&dev_attr_rw_reg.attr,
	&dev_attr_read_fw_rom.attr,
	&dev_attr_debug_th.attr,
	&dev_attr_cali_param.attr,
	&dev_attr_power_stat.attr,
	&dev_attr_press_thd.attr,
	NULL,
};

static const struct attribute_group cs_sys_attr_group = {
	.attrs = cs_sys_attrs,
	.name = "cs_press",
};

static int cs_sys_create(void)
{
	int ret = 0;
	struct kobject *kobj;

	kobj = kobject_create_and_add("cs_press", NULL);
	if (kobj == NULL) {
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_group(kobj, &cs_sys_attr_group);
	return 1;
}


static const struct file_operations cs_fops = {
	.owner		= THIS_MODULE,
	.read		= cs_read,
	.write		= cs_write,
	.llseek		= cs_lseek,
	.open		= cs_open,
	.release	= cs_close,
};
static struct miscdevice cs_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "ndt",
	.fops  = &cs_fops,
};

static int cs_open(struct inode *inode, struct file *file)
{
	g_cs_reg = 0;

	if (NULL == g_cs_client)
		return -1;
	else
		return 0;
}

static int cs_close(struct inode *inode, struct file *file)
{
	return 0;
}

static loff_t cs_lseek(struct file *file, loff_t offset, int whence)
{
	LOG("cs_lseek: %d\n", (int)offset);
	g_cs_reg = offset;
	return offset;
}

static ssize_t cs_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset)
{
	int err;
	char *kbuf = NULL;
	char reg;

	kbuf = kzalloc(count, GFP_KERNEL);
	if (!kbuf) {
		err = -ENOMEM;
		goto exit;
	}

	/*get reg addr buf[0]*/
	if (copy_from_user(&reg, buf, 1)) {
		err = -EFAULT;
		goto exit_kfree;
	}

	err = cs_i2c_read(reg, kbuf, count);
	if (err < 0)
		goto exit_kfree;

	if (copy_to_user(buf+1, kbuf, count))
		err = -EFAULT;

exit_kfree:
	kfree(kbuf);

exit:
	return err;
}

static ssize_t cs_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset)
{
	int err;
	char *kbuf = NULL;
	char reg;

	kbuf = kzalloc(count, GFP_KERNEL);
	if (!kbuf) {
		err = -ENOMEM;
		goto exit;
	}

	if (copy_from_user(&reg, buf, 1)
		|| copy_from_user(kbuf, buf+1, count)) {
		err = -EFAULT;
		goto exit_kfree;
	}

	err = cs_i2c_write(reg, kbuf, count);

exit_kfree:
	kfree(kbuf);

exit:
	return err;
}

/*---add eint detect start----*/
#ifdef INT_SET_EN

static void cs_irq_enable(void)
{
	if (cs_irq_flag == 0) {
		cs_irq_flag++; 
		enable_irq(cs_press_irq);
		LOG("Enable irq_flag=%d\n", cs_irq_flag);
	} else {
		LOG("cs_press Eint already enabled!\n");
	}

}
static void cs_irq_disable(void)
{
	if (cs_irq_flag == 1) {
		cs_irq_flag--;
		disable_irq_nosync(cs_press_irq);
		LOG("Disable irq_flag=%d\n", cs_irq_flag);
	} else {
		LOG("cs_press Eint already disabled!\n");
	}
}

static irqreturn_t cs_press_interrupt_handler(int irq, void *dev_id)
{
	cs_press_int_flag = 1;

	wake_up_interruptible(&cs_press_waiter);

	return IRQ_HANDLED;
}

unsigned char get_key_event(void){
	unsigned char rbuf[1];
	unsigned char addr;
	int len = 0;
	
	addr = IIC_KEY_EVENT;
	len = 1;
	rbuf[0] = 0x0;
	if (cs_i2c_read(addr, rbuf, len) <= 0) {
		LOG("reg=%d,buf[0]=%d,len=%d,err\n",
		addr, rbuf[0], len);
	}
	return rbuf[0];

}
unsigned int get_key_pressure(unsigned char addr){
	unsigned char rbuf[2];
	int len = 2;
	
	rbuf[0] = 0x0;
	rbuf[1] = 0x0;
	if (cs_i2c_read(addr, rbuf, len) <= 0) {
		LOG("reg=%d,buf[0]=%d,len=%d,err\n",
		addr, rbuf[0], len);
	}
	return ((rbuf[0]&0xff) +( (rbuf[1]<<8)&0xff00));

}
unsigned char get_key_stat(void){
	unsigned char rbuf[1];
	unsigned char addr;
	int len = 0;
	
	addr = IIC_KEY_STAT;
	len = 1;
	rbuf[0] = 0x0;
	if (cs_i2c_read(addr, rbuf, len) <= 0) {
		LOG("reg=%d,buf[0]=%d,len=%d,err\n",
		addr, rbuf[0], len);
	}
	return rbuf[0];

}
unsigned char clear_key_stat(void){
	unsigned char rbuf[1];
	unsigned char addr;
	int len = 0;
	
	addr = IIC_KEY_STAT;
	len = 1;
	rbuf[0] = 0x0f;
	if (cs_i2c_write(addr, rbuf, len) <= 0) {
		LOG("reg=%d,buf[0]=%d,len=%d,err\n",
		addr, rbuf[0], len);
		return 1;
	}
	return 0;

}


void report_key(void)
{
	static char clr_flg = 0;
	static char down_flg = 0;
	unsigned char stat = 0;
	unsigned char event = 0;
	unsigned int left_val = 0;
	unsigned int right_val = 0;

	wake_up_fw();

	stat = get_key_stat();
	if(stat == CS_PRESS_KEY_DOWN)
	{
		input_report_key(cs_input_dev, BTN_CS_PRESS, 1);
		input_sync(cs_input_dev);
		clear_key_stat();
		clr_flg = 0;
		down_flg = 1;
		LOG("Key down start!\n");
	}
	if(stat==CS_PRESS_KEY_MOVE && clr_flg==0)
	{
		clear_key_stat();
		clr_flg = 1;
	}
	if(stat==CS_PRESS_KEY_UP || (down_flg && stat==0))
	{
		input_report_key(cs_input_dev, BTN_CS_PRESS, 0);
		input_sync(cs_input_dev);
		clear_key_stat();
		clr_flg = 0;
		down_flg = 0;
		LOG("Key up end!\n");
	}

	event = get_key_event();
	if(event & 0x60){
		left_val = get_key_pressure(IIC_LEFT_KEY_PRESS);
		input_report_abs(cs_input_dev, ABS_X, left_val);
		input_sync(cs_input_dev);

		right_val = get_key_pressure(IIC_RIGHT_KEY_PRESS);
		input_report_abs(cs_input_dev, ABS_Y, right_val);
		input_sync(cs_input_dev);
	}
	LOG("stat:%d event:%d left:%d right:%d\n", stat, event, left_val, right_val);
}

static int cs_press_event_handler(void *unused)
{
	do {
		wait_event_interruptible(cs_press_waiter,
			cs_press_int_flag != 0);

		cs_press_int_flag = 0;

		report_key();

	} while (!kthread_should_stop());
	return 0;
}

void eint_init(void)
{
	int ret;

	cs_input_dev = input_allocate_device();
	if (cs_input_dev != NULL) {
		cs_input_dev->name = CS_CHRDEV_NAME;

		set_bit(EV_ABS, cs_input_dev->evbit);
		set_bit(EV_KEY, cs_input_dev->evbit);
		set_bit(BTN_CS_PRESS, cs_input_dev->keybit);

		input_set_abs_params(cs_input_dev, ABS_X, 0, 65535, 0, 0);
		input_set_abs_params(cs_input_dev, ABS_Y, 0, 65535, 0, 0);

		ret = input_register_device(cs_input_dev);
		if (ret != 0)
			LOG("input register device error = %d\n", ret);
	}
	init_waitqueue_head(&cs_press_waiter);

	kthread_run(cs_press_event_handler, 0, CS_CHRDEV_NAME);

}
#endif

int parse_dt(struct i2c_client *pdev)
{
	int ret = -1;
	int irq_gpio = 0;
	power_3v3 = regulator_get(&pdev->dev, "vdd_3v3");
    if(IS_ERR(power_3v3))
    {
       dev_err(&pdev->dev, "cs_press can not find the power!ret=%d\n", ret); 
    }
    else
    {
       	if (regulator_count_voltages(power_3v3) > 0) {
    		ret = regulator_set_voltage(power_3v3, 2800000, 3300000);
    		if (ret) {
    			LOG("Regulator set_vtg failed vdd ret=%d\n", ret);
    			return ret;
    		}

    		//ret = regulator_set_load(power_3v3, CURRENT_LOAD_UA);
    		//if (ret) {
    		//	LOG("Regulator set_vtg failed vdd ret=%d\n", ret);
    		//	return ret;
    		//}
    	}
    	//enable the 3v3 power
    	ret = regulator_enable(power_3v3);
    	if (ret) {
    		LOG("Regulator vdd enable failed ret=%d\n", ret);
    		return ret;
    	}
		atomic_set(&power_stat, POWER_ON);
    }

#ifdef INT_SET_EN
	cs_press_pinctrl = devm_pinctrl_get(&pdev->dev);
	if(cs_press_pinctrl == NULL)
	{
		LOG("get dev pinctrl failed!\n");
	}else{
		LOG("get dev pinctrl success:%p!\n", cs_press_pinctrl);
	}
	cs_press_irq_stat = pinctrl_lookup_state(cs_press_pinctrl, "cs_press_irq_pull_up");
	if(cs_press_irq_stat != NULL)
		LOG("check pinctrl state: %p\n", cs_press_irq_stat);
	else
		LOG("check pinctrl failed!\n");

	if(cs_press_pinctrl!=NULL && cs_press_irq_stat!=NULL)
	{
		ret = pinctrl_select_state(cs_press_pinctrl, cs_press_irq_stat);
		LOG("pinctrl select state=d\n", ret);
	}
	irq_gpio = of_get_named_gpio((&pdev->dev)->of_node,"press,irq-gpio",0);

    if(!gpio_is_valid(irq_gpio))
    {
        dev_err(&pdev->dev, "cspress request_irq IRQ fail");
    }
    else
    {
        ret = gpio_request(irq_gpio, "press,irq-gpio");
        if(ret)
        {
            dev_err(&pdev->dev, "cspress request_irq IRQ fail !,ret=%d.\n", ret);
        }else{
        
	        cs_press_irq = gpio_to_irq(irq_gpio);

			ret = gpio_direction_input(irq_gpio);

	        msleep(50);

	    	ret = request_irq(cs_press_irq,
			  (irq_handler_t)cs_press_interrupt_handler,
			  IRQF_TRIGGER_FALLING| IRQF_ONESHOT,
			  "CS_PRESS-eint", NULL);

	    	if (ret > 0) {
	    		ret = -1;
	    		dev_err(&pdev->dev, "cs_press request_irq IRQ fail !,ret=%d.\n", ret);
	    	}
        }
    	cs_irq_disable();
    }
#endif
	cs_press_rst = of_get_named_gpio((&pdev->dev)->of_node,"press,rst-gpio",0);
  	if(!gpio_is_valid(cs_press_rst))
      {
		dev_err(&pdev->dev, "cs_press request_rst fail");
	} else {
		ret = gpio_request(cs_press_rst, "press,rst-gpio");
	        if(ret)
	        {
	            dev_err(&pdev->dev, "cs_press request rst fail !,ret=%d.\n", ret);
	        }
	        ret = gpio_direction_output(cs_press_rst,0);
	        msleep(50);
		gpio_set_value(cs_press_rst, 0);
	}
	LOG("end---\n");
	return 0;
}
void cs_set_rst_pin(int val)
{
	if(gpio_is_valid(cs_press_rst)) {
		if (val == 0)
			gpio_set_value(cs_press_rst, 0);
		else if (val == 1)
			gpio_set_value(cs_press_rst, 1);
	}
}
void cs_rst_set(void)
{
	if(gpio_is_valid(cs_press_rst)) {
		rt_mutex_lock(&g_cs_client->adapter->bus_lock);

		gpio_set_value(cs_press_rst, 1);
		LOG("rst set 1\n");
		msleep(10);
		gpio_set_value(cs_press_rst, 0);
		LOG("rst set 0\n");

		rt_mutex_unlock(&g_cs_client->adapter->bus_lock);
	}
}
/*---add gpio setting end---*/


static void update_work_func(struct work_struct *worker)
{
	int ret = 0;

	if(atomic_read(&power_stat) != POWER_ON)
	{
		LOG("Device is power off,  stop to update firmware.\n");
		return;
	}
	atomic_set(&power_stat, POWER_PROTECT);
	ret = cs_fw_update();
	if(ret <= 0)
	{
		LOG("Firmware update failed! retry after %d ms!\n", CHECK_FW_DELAY_TIME);
		schedule_delayed_work(&update_worker, msecs_to_jiffies(CHECK_FW_DELAY_TIME));
	}else
	{
		LOG("Firmware status is OK, stop auto update firmware!\n");
	}
	atomic_set(&power_stat, POWER_ON);
}

static int cs_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	LOG("start\n");

	ret = misc_register(&cs_misc);

	g_cs_client = client;

	parse_dt(client);

#ifdef INT_SET_EN
	eint_init();
#endif

	INIT_DELAYED_WORK(&update_worker, update_work_func);
	schedule_delayed_work(&update_worker, msecs_to_jiffies(CHECK_FW_DELAY_TIME));
	LOG("update_work_func start,delay 2s.\n");


#ifdef I2C_CHECK_SCHEDULE
	INIT_DELAYED_WORK(&i2c_check_worker, i2c_check_func);
	schedule_delayed_work(&i2c_check_worker, msecs_to_jiffies(CHECK_DELAY_TIME));
	LOG("i2c_check_func start,delay 20s.\n");
#endif

	cs_irq_enable();

	cs_sys_create();

	LOG("end!\n");
	return 0;

}

static int cs_remove(struct i2c_client *client)
{
	if (NULL == g_cs_client)
		return 0;

	i2c_unregister_device(g_cs_client);
	misc_deregister(&cs_misc);
	g_cs_client = NULL;
	return 0;
}

static int cs_suspend(struct device *device)
{
	LOG("cs_suspend in!\n");

	//enable_irq_wake(cs_press_irq);

    atomic_set(&sys_stat, POWER_SUSPEND);

	LOG("cs_suspend out! power_stat=%d.\n", atomic_read(&power_stat));

	return 0;
}

static int cs_resume(struct device *device)
{
	LOG("cs_resume in!\n");
	atomic_set(&sys_stat, 0);

	//disable_irq_wake(cs_press_irq);

	LOG("cs_resume out! power_stat=%d.\n", atomic_read(&power_stat));

	return 0;
}

static const struct dev_pm_ops cs_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cs_suspend, cs_resume)
};


static const struct i2c_device_id cs_id_table[] = {
	{CS_CHRDEV_NAME, 0},
	{},
};

static struct of_device_id cs_match_table[] = {
	{ .compatible = "qcom,cs_press", },
	{},
};
MODULE_DEVICE_TABLE(i2c, cs_id_table);

static struct i2c_driver cs_driver = {
	.id_table = cs_id_table,
	.probe = cs_probe,
	.remove = cs_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = CS_CHRDEV_NAME,
		.of_match_table = cs_match_table,
		.pm = &cs_pm_ops,
	},
};

static int __init cs_init(void)
{
	int ret;

	LOG("start.\n");

	ret = i2c_add_driver(&cs_driver);
	if (ret < 0)
		LOG(" i2c_add_driver fail,status=%d\n", ret);

	return 0;
}

static void __exit cs_exit(void)
{
	i2c_del_driver(&cs_driver);
}

module_init(cs_init);
module_exit(cs_exit);
