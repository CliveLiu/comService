/*
 * Copyright (C) 2016 CliveLiu
 * Subject to the GNU Public License, version 2.
 *
 * Created By:		Clive Liu<ftdstudio1990@gmail.com>
 * Created Date:	2016-03-07
 *
 * ChangeList:
 * Created in 2016-03-07 by Clive;
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "debug.h"


#include "config.h"
#include "common.h"
#include "reg.h"

#include "i2c_util.h"

#include "serialDaemon.h"

#include "rgb24tobmp.h"
#include "calSperm.h"
#include "receiver.h"

// 获取图像帧对应的起始地址
#define FRAME2ADDR(addr,width,height,frame) \
    ((unsigned long)((unsigned long *)addr + width * height * frame))

/*
 * define the debug level of this file,
 * please see 'debug.h' for detail info
 */
DEBUG_SET_LEVEL(DEBUG_LEVEL_DEBUG);

typedef void (*receiver_cmd_callback)(receiver_st * r);

extern serialDaemon_t stSerialDaemon;

static receiver_st * pReceiver = NULL;
static bool isConnected = false;
static Result_cal st_result = {
    .u16sn = 0,
    .u16count = 0,
    .u16motility = 0,
    .u16Rsv1 = 0,
    .u16Rsv2 = 0,
    .u16Rsv3 = 0,
    .u16Rsv4 = 0,
    .u16Rsv5 = 0,
};

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****           CMD   CONNECT                               *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/
static void receiver_connect(receiver_st* r)
{
    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);

    r->write_cb(r);
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****           CMD   PERFORM                               *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/
static void receiver_perform(receiver_st* r)
{
//#define ADDR_BASE (~(0x3uL))
    DEBUG("called");

    char dirPath[255], outfile[255], tmpCmd[255];
    uint8_t val;
    uint32_t reg;
    unsigned long addr;
    uint32_t width, height;
    uint32_t i;
    int retries;;

    // reset DMA controller
    devmem_set32(REG_ADDR_CTRL, 0x1, 1);
    usleep(1);
    devmem_set32(REG_ADDR_CTRL, 0x0, 1);

    // start to capture picture
    devmem_set32(REG_ADDR_CTRL, 0x2, 1);

    devmem_readsl(REG_ADDR_TMR, (void *)&reg, 1);
    DEBUG("0x43C00008 reg:%u", reg);
    usleep((reg+300) * 1000);

    stSerialDaemon.ov5640->release_vcm_cb(stSerialDaemon.ov5640);

    devmem_readsl(REG_ADDR_X, (void *)&reg, 1);
    width = reg & 0x0000FFFF;
    DEBUG("0x43C00000 width:%u", width);

    devmem_readsl(REG_ADDR_Y, (void *)&reg, 1);
    height = reg & 0x0000FFFF;
    DEBUG("0x43C00004 height:%u", height);

    devmem_readsl(REG_ADDR_SAMPLE, (void *)&reg, 1);
    DEBUG("0x43C00018 reg:%u", reg);

    switch ( (uint8_t)r->rx_base.aParm[0] ) {
        case PERF_SAMPLE: {
            // create directory
            sprintf(dirPath, "/mnt/bmp_sample");
            if (!access(dirPath, F_OK)) {
                sprintf(tmpCmd, "rm -rf %s", dirPath);
                system(tmpCmd);
                sync();
            }
            mkdir(dirPath, S_IRWXU | S_IRWXG | S_IRWXO);
            DEBUG("DIR:%s", dirPath);

            // generate bmp files
            for (i = 0; i < reg ; ++i) {
                putchar('.');
                fflush(stdout);
                sprintf(outfile, "/mnt/bmp_sample/file%d.bmp", i);
                rgb24tobmp(FRAME2ADDR(0x18000000,width,height,i), outfile, width, height, 24);
            }
            sync();
            puts("");

            memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);

#if 0
            retries = 5;
            do {
                val = 0x8;
                pI2c->write_cb(pI2c, 0x3022, &val, 1);
            } while(--retries);
            break;
#endif
        } case PERF_MEASURE: {
            // Debug
            sprintf(outfile, "/mnt/file.bmp");
            if (!access(outfile, F_OK)) {
                sprintf(tmpCmd, "rm -rf %s", outfile);
                system(tmpCmd);
                sync();
            }
            rgb24tobmp(FRAME2ADDR(0x18000000, width, height, 0), outfile, width, height, 24);
            printf("Generating bmp files...\n");

            calSperm(0x18000000, width, height, reg, &st_result);

            r->tx_base.ucLen = 0x14;
            r->tx_base.aId[0] = 0x00;
            r->tx_base.aId[1] = 0x10;
            r->tx_base.cCmd = CMD_RESULT;
            r->tx_base.aParm[0] = (st_result.u16sn & 0xFF00) >> 8;
            r->tx_base.aParm[1] = st_result.u16sn & 0xFF;
            r->tx_base.aParm[2] = (st_result.u16count & 0xFF00) >> 8;
            r->tx_base.aParm[3] = st_result.u16count & 0xFF;
            r->tx_base.aParm[4] = (st_result.u16motility & 0xFF00) >> 8;
            r->tx_base.aParm[5] = st_result.u16motility & 0xFF;
            r->tx_base.aParm[6] = (st_result.u16Rsv1 & 0xFF00) >> 8;
            r->tx_base.aParm[7] = st_result.u16Rsv1 & 0xFF;
            r->tx_base.aParm[8] = (st_result.u16Rsv2 & 0xFF00) >> 8;
            r->tx_base.aParm[9] = st_result.u16Rsv2 & 0xFF;
            r->tx_base.aParm[10] = (st_result.u16Rsv3 & 0xFF00) >> 8;
            r->tx_base.aParm[11] = st_result.u16Rsv3 & 0xFF;
            r->tx_base.aParm[12] = (st_result.u16Rsv4 & 0xFF00) >> 8;
            r->tx_base.aParm[13] = st_result.u16Rsv4 & 0xFF;
            r->tx_base.aParm[14] = (st_result.u16Rsv5 & 0xFF00) >> 8;
            r->tx_base.aParm[15] = st_result.u16Rsv5 & 0xFF;
            break;
        } case PERF_AF: {
            stSerialDaemon.ov5640->af_ctrl_cb(stSerialDaemon.ov5640);

            memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);
#if 0
            retries = 5;
            do {
                val = 0x3;
                pI2c->write_cb(pI2c, 0x3022, &val, 1);
            } while(--retries);
            while (0x10 != val) {
                pI2c->read_cb(pI2c, 0x3029, &val, 1);
                usleep(5000);
            }
            INFO("AF Completely.");
            sleep(3); // 延时3s(CMOS对焦过程)
            retries = 5;
            do {
                val = 0x6;
                pI2c->write_cb(pI2c, 0x3022, &val, 1);
            } while(--retries);
#endif
            break;
        }
        default:
            DEBUG("no response perform_command");
            break;
    }

    r->write_cb(r);
}

static void receiver_result(receiver_st* r)
{
    r->tx_base.ucLen = 0x14;
    r->tx_base.aId[0] = 0x00;
    r->tx_base.aId[1] = 0x01;
    r->tx_base.cCmd = CMD_RESULT;
    r->tx_base.aParm[0] = (st_result.u16sn & 0xFF00) >> 8;
    r->tx_base.aParm[1] = st_result.u16sn & 0xFF;
    r->tx_base.aParm[2] = (st_result.u16count & 0xFF00) >> 8;
    r->tx_base.aParm[3] = st_result.u16count & 0xFF;
    r->tx_base.aParm[4] = (st_result.u16motility & 0xFF00) >> 8;
    r->tx_base.aParm[5] = st_result.u16motility & 0xFF;
    r->tx_base.aParm[6] = (st_result.u16Rsv1 & 0xFF00) >> 8;
    r->tx_base.aParm[7] = st_result.u16Rsv1 & 0xFF;
    r->tx_base.aParm[8] = (st_result.u16Rsv2 & 0xFF00) >> 8;
    r->tx_base.aParm[9] = st_result.u16Rsv2 & 0xFF;
    r->tx_base.aParm[10] = (st_result.u16Rsv3 & 0xFF00) >> 8;
    r->tx_base.aParm[11] = st_result.u16Rsv3 & 0xFF;
    r->tx_base.aParm[12] = (st_result.u16Rsv4 & 0xFF00) >> 8;
    r->tx_base.aParm[13] = st_result.u16Rsv4 & 0xFF;
    r->tx_base.aParm[14] = (st_result.u16Rsv5 & 0xFF00) >> 8;
    r->tx_base.aParm[15] = st_result.u16Rsv5 & 0xFF;

    r->write_cb(r);
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****           CMD   GET   LIST                            *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/
static void _get_mcu_version(receiver_st* r)
{
    #define MCU_VERSION 0x0001 // 每4位表示一节，HH,HL,LH,LL

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    r->tx_base.aParm[2] = MCU_VERSION >> 8;
    r->tx_base.aParm[3] = MCU_VERSION & 0xFF;

    r->write_cb(r);
}

static void _get_fpga_version(receiver_st* r)
{
    int reg;

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    devmem_readsl(REG_ADDR_VER, (void *)&reg, 1);
    r->tx_base.aParm[2] = reg >> 8;
    r->tx_base.aParm[3] = reg & 0xFF;

    r->write_cb(r);
}

static void _get_linux_version(receiver_st* r)
{
    #define LINUX_VERSION 0x0318 // 每4位表示一节，HH,HL,LH,LL

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    r->tx_base.aParm[2] = LINUX_VERSION >> 8;
    r->tx_base.aParm[3] = LINUX_VERSION & 0xFF;

    r->write_cb(r);
}

static void _get_hw_version(receiver_st* r)
{
    #define HW_VERSION 0x0001 // 每4位表示一节，HH,HL,LH,LL

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    r->tx_base.aParm[2] = HW_VERSION >> 8;
    r->tx_base.aParm[3] = HW_VERSION & 0xFF;

    r->write_cb(r);
}

static void _get_sensor_type(receiver_st* r)
{
    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    r->tx_base.aParm[2] = 0x0;
    // type: ov5640
    r->tx_base.aParm[3] = 0x1;

    r->write_cb(r);
}

static void _get_pic_x_start(receiver_st* r)
{
    int reg;

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    devmem_readsl(REG_ADDR_X, (void *)&reg, 1);
    r->tx_base.aParm[2] = (reg & 0xFF000000) >> 24;
    r->tx_base.aParm[3] = (reg & 0x00FF0000) >> 16;

    r->write_cb(r);
}

static void _get_pic_width(receiver_st* r)
{
    int reg;

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    devmem_readsl(REG_ADDR_X, (void *)&reg, 1);
    r->tx_base.aParm[2] = (reg & 0xFF00) >> 8;
    r->tx_base.aParm[3] = reg & 0x00FF;

    r->write_cb(r);
}

static void _get_pic_y_start(receiver_st* r)
{
    int reg;

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    devmem_readsl(REG_ADDR_Y, (void *)&reg, 1);
    r->tx_base.aParm[2] = (reg & 0xFF000000) >> 24;
    r->tx_base.aParm[3] = (reg & 0x00FF0000) >> 16;

    r->write_cb(r);
}

static void _get_pic_height(receiver_st* r)
{
    int reg;

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    devmem_readsl(REG_ADDR_Y, (void *)&reg, 1);
    r->tx_base.aParm[2] = (reg & 0xFF00) >> 8;
    r->tx_base.aParm[3] = reg & 0x00FF;

    r->write_cb(r);
}

static void _get_sample_time(receiver_st* r)
{
    int reg;

    DEBUG("called");

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1 - 3);

    devmem_readsl(REG_ADDR_TMR, (void *)&reg, 1);
    r->tx_base.aParm[2] = (reg & 0xFF00) >> 8;
    r->tx_base.aParm[3] = reg & 0x00FF;

    r->write_cb(r);
}

receiver_cmd_callback receiver_cmd_get_cb[5][16] = {
{
NULL,
_get_mcu_version,
_get_fpga_version,
_get_linux_version,
_get_hw_version,
NULL,
NULL,
_get_sensor_type
},
{
_get_pic_x_start,
_get_pic_width,
_get_pic_y_start,
_get_pic_height,
_get_sample_time
},
{NULL},
{NULL},
{NULL},
};

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****           CMD   SET   LIST                            *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/
static void _set_pic_x_start(receiver_st* r)
{
    DEBUG("called");

    uint32_t reg = 0;
    uint32_t tmp = 0;

    tmp = (uint8_t)r->rx_base.aParm[2] << 8;
    tmp |= (uint8_t)r->rx_base.aParm[3];

    if (tmp > 4096U) {
        tmp = 4096;
    }

    devmem_readsl(REG_ADDR_X, (void *)&reg, 1);
    reg &= 0x0000FFFF;
    reg |= tmp << 16;

    devmem_set32(REG_ADDR_X, reg, 1);

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);

    r->write_cb(r);
}

static void _set_pic_width(receiver_st* r)
{
    DEBUG("called");

    uint32_t reg = 0;
    uint32_t tmp = 0;

    tmp = (uint8_t)r->rx_base.aParm[2] << 8;
    tmp |= (uint8_t)r->rx_base.aParm[3];

    devmem_readsl(REG_ADDR_X, (void *)&reg, 1);
    reg &= 0xFFFF0000;
    reg |= tmp;

    devmem_set32(REG_ADDR_X, reg, 1);

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);

    r->write_cb(r);
}

static void _set_pic_y_start(receiver_st* r)
{
    DEBUG("called");

    uint32_t reg = 0;
    uint32_t tmp = 0;

    tmp = (uint8_t)r->rx_base.aParm[2] << 8;
    tmp |= (uint8_t)r->rx_base.aParm[3];

    if (tmp > 2048U) {
        tmp = 2048;
    }

    devmem_readsl(REG_ADDR_Y, (void *)&reg, 1);
    reg &= 0x0000FFFF;
    reg |= tmp << 16;

    devmem_set32(REG_ADDR_Y, reg, 1);

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);

    r->write_cb(r);
}

static void _set_pic_height(receiver_st* r)
{
    DEBUG("called");

    uint32_t reg = 0;
    uint32_t tmp = 0;

    tmp = (uint8_t)r->rx_base.aParm[2] << 8;
    tmp |= (uint8_t)r->rx_base.aParm[3];

    devmem_readsl(REG_ADDR_Y, (void *)&reg, 1);
    reg &= 0xFFFF0000;
    reg |= tmp;

    devmem_set32(REG_ADDR_Y, reg, 1);

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);

    r->write_cb(r);
}

static void _set_sample_time(receiver_st* r)
{
    DEBUG("called");

    uint32_t reg = 0;

    reg |= (uint8_t)r->rx_base.aParm[2] << 8;
    reg |= (uint8_t)r->rx_base.aParm[3];

    if (reg > 65535U) {
        reg = 0xFFFF;
    }

    devmem_set32(REG_ADDR_TMR, reg, 1);

    memcpy(&r->tx_base, &r->rx_base, r->rx_base.ucLen + 1);

    r->write_cb(r);
}

receiver_cmd_callback receiver_cmd_set_cb[5][16] = {
{NULL},
{
_set_pic_x_start,
_set_pic_width,
_set_pic_y_start,
_set_pic_height,
_set_sample_time
},
{NULL},
{NULL},
{NULL},
};

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****           MSG   P A R S E R                           *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/
static void receiver_parse(receiver_st* r)
{
    int pos;
    unsigned char checksums;
    receiver_cmd_callback cmd_cb = NULL;

    //DEBUG("called");

#ifdef CONFIG_ENABLE_DEBUG
    printf("==========\n" \
           "Recv data as follows:\n");
    for (pos = 0; pos < RX_MAX_SIZE; pos++) {
        printf("r->in[%d]:0x%x\n", pos, (uint8_t)r->in[pos]);
    }
#endif

    for (checksums = 0, pos = POS_LEN; pos < r->in[POS_LEN] + 2; pos++) {
        checksums += (unsigned char)r->in[pos];
    }
    if (checksums != (unsigned char)r->in[RX_MAX_SIZE - 1]) {
        ERR("Wrong checksums, cal checksums:%x", checksums);
        return;
    }

    r->rx_base.ucChecksums = (unsigned char)r->in[POS_CHECKSUMS];
    r->rx_base.ucLen = (unsigned char)r->in[POS_LEN];
    r->rx_base.aId[ID_1ST] = r->in[POS_ID_1ST];
    r->rx_base.aId[ID_2ND] = r->in[POS_ID_2ND];
    r->rx_base.cCmd = r->in[POS_CMD];
    for (pos = 0; pos < (r->in[POS_LEN] - 2 - 1 - 1); ++pos) {
        r->rx_base.aParm[pos] = r->in[POS_CMD + pos + 1];
    }

    switch (r->rx_base.cCmd) {
        case CMD_CONNECT: {
            DEBUG("recv cmd: CMD_CONNECT");
            //do something
            isConnected = true;
            receiver_connect(r);
            break;
        }
        case CMD_PERFORM: {
            DEBUG("recv cmd: CMD_PERFORM");
            //do something
            if (isConnected) {
                receiver_perform(r);
            }
            break;
        }
        case CMD_RESULT: {
            DEBUG("recv cmd: CMD_RESULT");
            if (isConnected) {
                receiver_result(r);
            }
            break;
        }
        case CMD_SET: {
            DEBUG("recv cmd: CMD_SET");

            if (!isConnected) {
                break;
            }

            cmd_cb = receiver_cmd_set_cb[(uint8_t)r->rx_base.aParm[0]][(uint8_t)r->rx_base.aParm[1]];
            //do something
            if (NULL != cmd_cb) {
                cmd_cb(r);
            }
            break;
        }
        case CMD_GET: {
            DEBUG("recv cmd: CMD_GET");

            if (!isConnected) {
                break;
            }

            cmd_cb = receiver_cmd_get_cb[(uint8_t)r->rx_base.aParm[0]][(uint8_t)r->rx_base.aParm[1]];
            //do something
            if (NULL != cmd_cb) {
                cmd_cb(r);
            }
            break;
        }
        default: {
            ASSERT();
            break;
        }
    }
}

static int receiver_write(receiver_st* r)
{
    char buf[TX_MAX_SIZE];
    int retVal;
    int i = 0, j = 0;
    uint8_t checksums = 0;

    buf[i++] = HEADER_1ST;
    buf[i++]= HEADER_2ND;

    buf[i++] = r->tx_base.ucLen;
    buf[i++] = r->tx_base.aId[0];
    buf[i++] = r->tx_base.aId[1];
    buf[i++] = r->tx_base.cCmd;

    for (j = 0; j < (r->tx_base.ucLen - 2 - 1 - 1); j++) {
        buf[i++] = r->tx_base.aParm[j];
    }

    for (j = 2; j < (r->tx_base.ucLen + 2); j++) {
        printf(" 0x%x,",(uint8_t)buf[j]);
        checksums += buf[j];
    }
    buf[i++] = checksums;

#ifdef CONFIG_ENABLE_DEBUG
    printf("==========\n");
    printf("Tx data as follows:");
    int k;
    for (k = 0; k < i; k++) {
        printf(" 0x%x,",(uint8_t)buf[k]);
    }
    printf("\n");
#endif

    retVal = write(r->fd, buf, i);
    if (retVal < 0) {
        ERR("Error on MSG write :%s", strerror(errno));
        return -1;
    }

    return 0;
}

static int receiver_read(receiver_st* r, char c)
{
    switch(r->pos) {
        case 0: {
            if ((uint8_t)c != HEADER_1ST) {
                ERR("Wrong header, c=0x%x",(uint8_t)c);
                return -1;
            }
            break;
        }
        case 1: {
            if ((uint8_t)c != HEADER_2ND) {
                r->pos = 0;
                ERR("Wrong header, c=0x%x",(uint8_t)c);
                return -1;
            }
            break;
        }
        default:
            break;
    }

    r->in[r->pos] = c;
    r->pos += 1;

    if (RX_MAX_SIZE == r->pos) {
        receiver_parse(r);
        r->pos = 0;
    }

    return 0;
}

static void receiver_init(receiver_st*  r)
{
    DEBUG("called");

    memset(r, 0, sizeof(*r));

    r->read_cb = receiver_read;
    r->write_cb = receiver_write;
}

receiver_st * receiver_instance(void)
{
    DEBUG("called");

    if (pReceiver){
        INFO("Object already instance");
        return pReceiver;
    }

    pReceiver = (receiver_st *)malloc(sizeof(*pReceiver));
    if (NULL == pReceiver) {
        ERR("Fail to alloc memory");
        return NULL;
    }

    receiver_init(pReceiver);

    return pReceiver;
}

void receiver_destroy(receiver_st * r)
{
    DEBUG("called");

    if (NULL == pReceiver) {
        INFO("Object already destroy");
        return;
    }

    free(pReceiver);
    pReceiver = NULL;
}

