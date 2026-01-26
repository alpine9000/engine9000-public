/*
Copyright (c) 2022-2024 Rupert Carmichael
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// NEC uPD4990A

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "geo_rtc.h"
#include "geo_serial.h"

// 68K @ 12MHz == 12,000,000Hz
#define CYCS_68K_PER_SECOND 12000000

// Timing Pulse Mode
#define TPMODE_RUN  0x00
#define TPMODE_STOP 0x01

/* 52-bit shift register broken into command and data sections (4-bit, 48-bit)
   ========================================================================
   | CMD | Y10 | Y1 | MO | WD | D10 | D1 | H10 | H1 | M10 | M1 | S10 | S1 |
   ========================================================================
   CMD is 4 bits. Year, Day, Hour, Minute, and Second are separated into a 10s
   and a 1s block of 4 bits each, while Month and Weekday are single 4 bit
   blocks.
*/
static uint8_t cmdreg = 0;
static uint64_t datareg = 0;

// 1Hz Interval Timer cycles
static uint32_t cycs = 0;

// Timing Pulse Interval
static uint32_t tpinterval = 0;

// Timing Pulse Counter
static uint32_t tpcounter = 0;

// Timing Pulse Output Mode
static uint8_t tpmode = 0;

// Function Mode
static uint8_t fmode = 0;

// Time Counter - units separated into variables for ease of use with time.h
static uint32_t year = 0;
static uint32_t month = 0;
static uint32_t weekday = 0;
static uint32_t day = 0;
static uint32_t hour = 0;
static uint32_t minute = 0;
static uint32_t second = 0;

// CLK (Clock) and STB (Strobe) values from previous write operations
static uint8_t clk = 0;
static uint8_t stb = 0;

// 1Hz Interval Timer and Timing Pulse output values
static uint8_t timer_1hz = 0;
static uint8_t tp = 0;

void geo_rtc_init(void) {
    // Zero the 1Hz Interval Timer and Command register
    cycs = 0;
    cmdreg = 0;

    // Seed the time data with the current system time
    time_t rawtime;
    time(&rawtime);
    struct tm *timeinfo = localtime(&rawtime);

    /* Data types are: second, minute, hour, day, weekday, month, year
       A 24-hour system and the last two digits of the Gregorian year are used
       Leap years are multiples of 4
       Data is represented in BCD notation except for months, which use hex.
       Weekdays are 0-6, with Sunday being 0 and Saturday being 6.
    */
    year = timeinfo->tm_year % 100;
    month = timeinfo->tm_mon + 1;
    weekday = timeinfo->tm_wday;
    day = timeinfo->tm_mday;
    hour = timeinfo->tm_hour;
    minute = timeinfo->tm_min;
    second = timeinfo->tm_sec;

    // Zero the CLK and STB inputs
    clk = 0;
    stb = 0;

    // Zero the TP output, mode, and counter
    tp = 0;
    tpmode = 0;
    tpcounter = 0;

    // Initialize the Timing Pulse interval to one second
    tpinterval = CYCS_68K_PER_SECOND;
}

uint8_t geo_rtc_rd(void) {
    /* In Register Hold Mode and Time Read Mode, the DATA OUT pin is set to the
       1Hz Interval Timer value, otherwise it is set to the lowest bit of the
       Shift Register.
    */
    uint8_t dataout =
        ((fmode == 0 || fmode == 3) ? timer_1hz : (datareg & 0x01)) << 1;

    /* Return the values of the DATA OUT and TP pins -- this is set up for easy
       use in the Neo Geo MVS' REG_STATUS_A reads.
    */
    return dataout | tp;
}

void geo_rtc_wr(uint8_t data) {
    // Check for CLK and STB going from low to high
    int clk_lowhigh = !clk && (data & 0x02);
    int stb_lowhigh = !stb && (data & 0x04);

    // Update CLK and STB
    clk = data & 0x02;
    stb = data & 0x04;

    // If STB is going low to high, process a command
    if (stb_lowhigh) {
        switch (cmdreg & 0x0f) {
            case 0x00: { // Register Hold -- DATA OUT = 1Hz
                /* The 48-bit shift register is held. The command register is
                   not held.
                */
                fmode = 0;
                tpinterval = CYCS_68K_PER_SECOND / 64;
                break;
            }
            case 0x01: { // Register Shift -- DATA OUT = [LSB] (0/1)
                fmode = 1;
                break;
            }
            case 0x02: { // Time Set & Counter Hold -- DATA OUT = [LSB] (0/1)
                fmode = 2;

                /* Data is transferred from the 48-bit shift register to the
                   time counter.
                */
                second =
                    (datareg & 0x0f) + (((datareg >> 4) & 0x0f) * 10);
                minute =
                    ((datareg >> 8) & 0x0f) + (((datareg >> 12) & 0x0f) * 10);
                hour =
                    ((datareg >> 16) & 0x0f) + (((datareg >> 20) & 0x0f) * 10);
                day =
                    ((datareg >> 24) & 0x0f) + (((datareg >> 28) & 0x0f) * 10);
                weekday =
                    ((datareg >> 32) & 0x0f);
                month =
                    ((datareg >> 36) & 0x0f);
                year =
                    ((datareg >> 40) & 0x0f) + (((datareg >> 44) & 0x0f) * 10);
                break;
            }
            case 0x03: { // Time Read -- DATA OUT = 1Hz
                fmode = 3;

                /* Data is transferred from the time counter to the 48-bit
                   shift register.
                */
                datareg = 0; // Reset it
                datareg |= second % 10;
                datareg |= (second / 10) << 4;
                datareg |= (minute % 10) << 8;
                datareg |= (minute / 10) << 12;
                datareg |= (hour % 10) << 16;
                datareg |= (hour / 10) << 20;
                datareg |= (day % 10) << 24;
                datareg |= (day / 10) << 28;
                datareg |= ((uint64_t)weekday) << 32;
                datareg |= ((uint64_t)month) << 36;
                datareg |= ((uint64_t)year % 10) << 40;
                datareg |= ((uint64_t)year / 10) << 44;
                break;
            }
            case 0x04: { // TP = 64Hz
                tpinterval = CYCS_68K_PER_SECOND / 64;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x05: { // TP = 256Hz
                tpinterval = CYCS_68K_PER_SECOND / 256;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x06: { // TP = 2048Hz
                tpinterval = CYCS_68K_PER_SECOND / 2048; // Some precision loss
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x07: { // TP = 4096Hz
                tpinterval = CYCS_68K_PER_SECOND / 4096; // Some precision loss
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x08: { // TP = 1s interval set (counter reset & start)
                tpinterval = CYCS_68K_PER_SECOND;
                tpcounter = 0;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x09: { // TP = 10s interval set (counter reset & start)
                tpinterval = CYCS_68K_PER_SECOND * 10;
                tpcounter = 0;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x0a: { // TP = 30s interval set (counter reset & start)
                tpinterval = CYCS_68K_PER_SECOND * 30;
                tpcounter = 0;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x0b: { // TP = 60s interval set (counter reset & start)
                tpinterval = CYCS_68K_PER_SECOND * 60;
                tpcounter = 0;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x0c: { // Interval Output Flag Reset
                /* The interval signal to the TP pin is reset. The interval
                   timer counter continues operation.
                */
                tp = 0;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x0d: { // Interval Timer Clock Run
                /* The timer for outputting interval signals is reset, then
                   started.
                */
                tpcounter = 0;
                tpmode = TPMODE_RUN;
                break;
            }
            case 0x0e: { // Interval Timer Clock Stop
                /* The timer for outputting interval signals is stopped. The
                   output status does not change.
                */
                tpmode = TPMODE_STOP;
                break;
            }
            case 0x0f: { // TEST MODE SET
                break;
            }
        }
    }

    /* If this is not an STB write and the CLK line is going from low to high,
       shift a new bit into the shift register.
    */
    if (!stb && clk_lowhigh) {
        // Command Register
        uint64_t shift_out = cmdreg & 0x01 ? 0x800000000000 : 0x00;
        cmdreg = ((cmdreg >> 1) & 0x07) | ((data & 0x01) << 3);

        // Data Register
        if (fmode == 1) // Data Register - only in Register Shift Mode
            datareg = ((datareg >> 1) & 0x7fffffffffff) | shift_out;
    }
}

static inline void geo_rtc_clk_inc(void) {
    if (++second < 60)
        return;
    else
        second = 0;

    if (++minute < 60)
        return;
    else
        minute = 0;

    if (++hour < 24)
        return;
    else
        hour = 0;

    if (++weekday >= 7)
        weekday = 0;

    /* Thirty days hath September, April, June, and November. All the rest have
       thirty-one, except February, twenty-eight days clear, and twenty-nine in
       each leap year.
    */
    uint32_t monthdays[12] = {
        31, (year % 4 ? 28 : 29), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (++day <= monthdays[month - 1])
        return;
    else
        day = 1; // No 0th day

    if (++month <= 12)
        return;
    else
        month = 1; // No 0th month

    if (++year < 100)
        return;
    year = 0; // If you really trigger this, you win at life.
}

void geo_rtc_sync(unsigned scycs) {
    cycs += scycs;

    if (cycs >= CYCS_68K_PER_SECOND) {
        // Increment the clock by one second
        geo_rtc_clk_inc();

        cycs -= CYCS_68K_PER_SECOND;
    }

    /* The 1Hz Interval Timer value is output at 50% duty. Set it high for the
       second half of the interval.
    */
    timer_1hz = cycs >= (CYCS_68K_PER_SECOND >> 1);

    if (tpmode == TPMODE_RUN) {
        tpcounter += scycs;

        if (tpcounter >= tpinterval)
            tpcounter -= tpinterval;

        /* The TP value is set low for the first half of the interval, high for
           the second half (50% duty).
        */
        tp = tpcounter >= (tpinterval >> 1);
    }
}

void geo_rtc_state_load(uint8_t *st) {
    cmdreg = geo_serial_pop8(st);
    datareg = geo_serial_pop64(st);
    cycs = geo_serial_pop32(st);
    tpinterval = geo_serial_pop32(st);
    tpcounter = geo_serial_pop32(st);
    tpmode = geo_serial_pop8(st);
    fmode = geo_serial_pop8(st);
    year = geo_serial_pop32(st);
    month = geo_serial_pop32(st);
    weekday = geo_serial_pop32(st);
    day = geo_serial_pop32(st);
    hour = geo_serial_pop32(st);
    minute = geo_serial_pop32(st);
    second = geo_serial_pop32(st);
    clk = geo_serial_pop8(st);
    stb = geo_serial_pop8(st);
    timer_1hz = geo_serial_pop8(st);
    tp = geo_serial_pop8(st);
}

void geo_rtc_state_save(uint8_t *st) {
    geo_serial_push8(st, cmdreg);
    geo_serial_push64(st, datareg);
    geo_serial_push32(st, cycs);
    geo_serial_push32(st, tpinterval);
    geo_serial_push32(st, tpcounter);
    geo_serial_push8(st, tpmode);
    geo_serial_push8(st, fmode);
    geo_serial_push32(st, year);
    geo_serial_push32(st, month);
    geo_serial_push32(st, weekday);
    geo_serial_push32(st, day);
    geo_serial_push32(st, hour);
    geo_serial_push32(st, minute);
    geo_serial_push32(st, second);
    geo_serial_push8(st, clk);
    geo_serial_push8(st, stb);
    geo_serial_push8(st, timer_1hz);
    geo_serial_push8(st, tp);
}
