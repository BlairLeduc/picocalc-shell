/* test_sd.c — the card driver, src/port/sd.c, against a model of a card
 * in SPI mode (fake_sd.c). Multi-block reads and writes (CMD18 and
 * CMD12, CMD25 and its stop token) must move the right blocks in one
 * command, on SDHC's block addresses and SDSC's byte addresses; an error
 * mid-transfer must still stop it, so the card is left between commands;
 * and a card that slows down or stops must not keep sd.c past the
 * southbridge's 2.5 s (design.md §4.2), counted in the model's time. */

#include <stdio.h>
#include <string.h>

#include "fake_sd.h"
#include "sd.h"
#include "test_util.h"

/* sd.c's worst case, 2 s, and a millisecond of bytes on the bus; below
 * the southbridge's 2.5 s. */
#define LIMIT_US 2001000u

static uint8_t buf[64 * SD_BLOCK];

static int start(bool sdsc) {
    fake_sd_reset();
    fake_sd.sdsc = sdsc;
    bool hc = !sdsc;
    CHECK(sd_init(NULL, &hc) == SD_OK, "init, %s", sdsc ? "SDSC" : "SDHC");
    CHECK(hc == !sdsc, "high capacity %d", hc);
    memset(fake_sd_commands, 0, sizeof fake_sd_commands);
    return 0;
}

static uint8_t pattern(uint32_t block, unsigned i, unsigned salt) {
    return (uint8_t)(block * 7u + i * 13u + salt);
}

static void fill(uint32_t block, uint32_t count, unsigned salt) {
    for (uint32_t b = 0; b < count; b++)
        for (unsigned i = 0; i < SD_BLOCK; i++) buf[b * SD_BLOCK + i] = pattern(block + b, i, salt);
}

/* The model's blocks from..from+count-1 hold the pattern; 0 if so. */
static int on_card(uint32_t from, uint32_t count, unsigned salt) {
    for (uint32_t b = from; b < from + count; b++)
        for (unsigned i = 0; i < SD_BLOCK; i++)
            if (fake_sd_data[b][i] != pattern(b, i, salt)) return (int)b + 1;
    return 0;
}

static bool blank(uint32_t block) {
    for (unsigned i = 0; i < SD_BLOCK; i++)
        if (fake_sd_data[block][i]) return false;
    return true;
}

static int test_one_block(void) {
    for (int sdsc = 0; sdsc < 2; sdsc++) {
        if (start(sdsc)) return 1;
        fill(5, 1, 1);
        CHECK(sd_write_blocks(5, buf, 1) == SD_OK, "write one");
        CHECK(fake_sd_commands[24] == 1 && fake_sd_commands[25] == 0, "CMD24 for one block");
        CHECK(on_card(5, 1, 1) == 0, "the block written");
        memset(buf, 0, SD_BLOCK);
        CHECK(sd_read_blocks(5, buf, 1) == SD_OK, "read one");
        CHECK(fake_sd_commands[17] == 1 && fake_sd_commands[18] == 0, "CMD17 for one block");
        CHECK(buf[3] == pattern(5, 3, 1) && buf[511] == pattern(5, 511, 1), "the block read");
        CHECK(fake_sd_idle() && fake_sd_faults == 0, "idle, %lu faults", fake_sd_faults);
    }
    return 0;
}

static int test_many_blocks(void) {
    for (int sdsc = 0; sdsc < 2; sdsc++) {
        if (start(sdsc)) return 1;
        fill(10, 32, 2);
        CHECK(sd_write_blocks(10, buf, 32) == SD_OK, "write 32, sdsc %d", sdsc);
        CHECK(fake_sd_commands[25] == 1 && fake_sd_commands[24] == 0,
              "one CMD25: %u, CMD24 %u", fake_sd_commands[25], fake_sd_commands[24]);
        CHECK(on_card(10, 32, 2) == 0, "block %d wrong", on_card(10, 32, 2) - 1);
        CHECK(blank(9) && blank(42), "neighbours untouched");
        CHECK(fake_sd_idle() && fake_sd_faults == 0, "after the write: %lu faults", fake_sd_faults);

        memset(buf, 0, sizeof buf);
        CHECK(sd_read_blocks(10, buf, 32) == SD_OK, "read 32");
        CHECK(fake_sd_commands[18] == 1 && fake_sd_commands[12] == 1 && fake_sd_commands[17] == 0,
              "one CMD18 and its CMD12");
        int bad = -1;
        for (uint32_t b = 0; b < 32 && bad < 0; b++)
            for (unsigned i = 0; i < SD_BLOCK; i++)
                if (buf[b * SD_BLOCK + i] != pattern(10 + b, i, 2)) { bad = (int)b; break; }
        CHECK(bad < 0, "block %d read wrong", bad);
        CHECK(fake_sd_idle() && fake_sd_faults == 0, "after the read: %lu faults", fake_sd_faults);

        /* Two blocks, the least that streams; then one more command
         * works, so the stop left the card ready. */
        CHECK(sd_read_blocks(41, buf, 2) == SD_OK, "read 2");
        CHECK(buf[0] == pattern(41, 0, 2) && buf[SD_BLOCK] == 0, "41 written, 42 blank");
        CHECK(sd_read(10, buf) == SD_OK && buf[1] == pattern(10, 1, 2), "a read after");
        CHECK(fake_sd_idle() && fake_sd_faults == 0, "%lu faults", fake_sd_faults);
    }
    return 0;
}

static int test_errors(void) {
    if (start(false)) return 1;
    fill(0, 64, 3);
    CHECK(sd_write_blocks(0, buf, 64) == SD_OK, "write 64");

    fake_sd.read_error = 15;
    CHECK(sd_read_blocks(10, buf, 32) == SD_REJECTED, "an error token mid-read");
    CHECK(fake_sd_commands[12] == 1, "stopped with CMD12");
    CHECK(fake_sd_idle() && fake_sd_faults == 0, "read error: %lu faults", fake_sd_faults);
    fake_sd.read_error = FAKE_SD_NONE;
    CHECK(sd_read_blocks(50, buf, 4) == SD_OK && buf[0] == pattern(50, 0, 3), "a read after");

    fake_sd.write_error = 13;
    fill(10, 8, 4);
    CHECK(sd_write_blocks(10, buf, 8) == SD_REJECTED, "a write error mid-write");
    CHECK(on_card(10, 3, 4) == 0, "the blocks before it written");
    CHECK(on_card(13, 5, 3) == 0, "it and the ones after untouched");
    CHECK(fake_sd_idle() && fake_sd_faults == 0, "write error: %lu faults", fake_sd_faults);
    fake_sd.write_error = FAKE_SD_NONE;
    CHECK(sd_write_blocks(10, buf, 8) == SD_OK && on_card(10, 8, 4) == 0, "a write after");
    CHECK(fake_sd_idle() && fake_sd_faults == 0, "%lu faults", fake_sd_faults);
    return 0;
}

/* A slow card within the bound is only slow; past it, the transfer is
 * stopped and the card left ready for the next. */
static int test_slow(void) {
    if (start(false)) return 1;
    fill(0, 32, 5);

    fake_sd.busy_us = 20000;
    uint64_t t = fake_sd_now;
    CHECK(sd_write_blocks(0, buf, 32) == SD_OK, "32 blocks at 20 ms each");
    CHECK(fake_sd_now - t > 32u * 20000u, "the busy was waited for");
    fake_sd.read_us = 20000;
    CHECK(sd_read_blocks(0, buf, 32) == SD_OK && buf[0] == pattern(0, 0, 5), "read the same");

    fake_sd.busy_us = 100000;
    t = fake_sd_now;
    sd_status_t st = sd_write_blocks(0, buf, 32);
    uint64_t took = fake_sd_now - t;
    CHECK(st == SD_NO_REPLY, "100 ms a block: %d", st);
    CHECK(took < LIMIT_US, "write gave up after %llu us", (unsigned long long)took);
    CHECK(fake_sd_idle() && fake_sd_faults == 0, "slow write: %lu faults", fake_sd_faults);

    /* Each block's busy within SD_READY_US, but all of them past the
     * transfer's 1 s: the worst case, a wait for the last block, then
     * the stop. */
    fake_sd.busy_us = 400000;
    t = fake_sd_now;
    st = sd_write_blocks(0, buf, 32);
    took = fake_sd_now - t;
    CHECK(st == SD_NO_REPLY, "400 ms a block: %d", st);
    CHECK(took < LIMIT_US, "write gave up after %llu us", (unsigned long long)took);
    CHECK(fake_sd_idle() && fake_sd_faults == 0, "slower write: %lu faults", fake_sd_faults);

    fake_sd.read_us = 100000;
    t = fake_sd_now;
    st = sd_read_blocks(0, buf, 32);
    took = fake_sd_now - t;
    CHECK(st == SD_NO_REPLY, "100 ms a block: %d", st);
    CHECK(took < LIMIT_US, "read gave up after %llu us", (unsigned long long)took);
    CHECK(fake_sd_idle() && fake_sd_faults == 0, "slow read: %lu faults", fake_sd_faults);

    fake_sd.busy_us = fake_sd.read_us = 5;
    CHECK(sd_read_blocks(0, buf, 32) == SD_OK && buf[0] == pattern(0, 0, 5), "a read after");
    return 0;
}

/* A card that stops answering: every call comes back inside the bound. */
static int test_stuck(void) {
    if (start(false)) return 1;
    fake_sd.stuck = true;
    uint64_t t = fake_sd_now;
    CHECK(sd_write_blocks(0, buf, 8) == SD_NO_REPLY, "stuck write");
    uint64_t took = fake_sd_now - t;
    CHECK(took < LIMIT_US, "stuck write took %llu us", (unsigned long long)took);

    if (start(false)) return 1;
    fake_sd.read_us = 10000000;
    t = fake_sd_now;
    CHECK(sd_read_blocks(0, buf, 8) == SD_NO_REPLY, "stuck read");
    took = fake_sd_now - t;
    CHECK(took < LIMIT_US, "stuck read took %llu us", (unsigned long long)took);

    /* Every wait used up in turn: busy when called, slow blocks to the
     * transfer's bound, then busy past the stop. The command's wait is
     * inside the transfer's bound, or the four would stack to 2.5 s. */
    if (start(false)) return 1;
    fake_sd.busy_us = 400000;
    fake_sd.stop_busy_us = 10000000;
    fake_sd_busy_for(490000);
    t = fake_sd_now;
    CHECK(sd_write_blocks(0, buf, 8) == SD_NO_REPLY, "write, every wait used");
    took = fake_sd_now - t;
    CHECK(took < LIMIT_US, "write, every wait used, took %llu us", (unsigned long long)took);

    if (start(false)) return 1;
    fake_sd.read_us = 10000000;
    fake_sd.stop_busy_us = 10000000;
    fake_sd_busy_for(490000);
    t = fake_sd_now;
    CHECK(sd_read_blocks(0, buf, 8) == SD_NO_REPLY, "read, every wait used");
    took = fake_sd_now - t;
    CHECK(took < LIMIT_US, "read, every wait used, took %llu us", (unsigned long long)took);
    return 0;
}

int main(void) {
    int (*tests[])(void) = { test_one_block, test_many_blocks, test_errors, test_slow, test_stuck };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++)
        if (tests[i]() != 0) return 1;
    TEST_DONE();
}
