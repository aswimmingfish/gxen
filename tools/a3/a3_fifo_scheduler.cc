/*
 * A3 FIFO scheduler
 *
 * Copyright (c) 2012-2013 Yusuke Suzuki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdint.h>
#include "a3.h"
#include "a3_fifo_scheduler.h"
#include "a3_context.h"
#include "a3_registers.h"
#include "a3_device.h"
#include "a3_device_bar1.h"
#include "a3_ignore_unused_variable_warning.h"
namespace a3 {

fifo_scheduler::fifo_scheduler(const boost::posix_time::time_duration& wait)
    : wait_(wait)
    , thread_()
    , mutex_()
    , queue_()
{
}

fifo_scheduler::~fifo_scheduler() {
    stop();
}

void fifo_scheduler::start() {
    if (thread_) {
        stop();
    }
    thread_.reset(new boost::thread(&fifo_scheduler::run, this));
}

void fifo_scheduler::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
    }
}

void fifo_scheduler::enqueue(context* ctx, const command& cmd) {
    A3_SYNCHRONIZED(mutex_) {
        queue_.push(fire_t(ctx, cmd));
    }
}

void fifo_scheduler::run() {
    context* current = NULL;
    bool wait = false;
    fire_t handle;
    while (true) {
        A3_SYNCHRONIZED(mutex_) {
            if (!wait && !queue_.empty()) {
                wait = true;
                handle = queue_.front();
                queue_.pop();
            }
        }

        bool will_be_sleep = true;
        if (wait) {
            A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                if (current == handle.first || !device::instance()->is_active()) {
                    if (current != handle.first) {
                        // acquire GPU
                        // context change.
                        // To ensure all previous fires should be submitted, we flush BAR1.
#if 0
                        registers::accessor regs;
                        regs.write32(0x070000, 0x00000001);
                        if (!regs.wait_eq(0x070000, 0x00000002, 0x00000000)) {
                            A3_LOG("BAR1 flush failed\n");
                        }
#endif
#ifndef A3_BUSY_LOOP
                        boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
                        if (device::instance()->is_active()) {
                            goto out;
                        }
#endif
                        current = handle.first;
                        const bool res = device::instance()->try_acquire_gpu(current);
                        ignore_unused_variable_warning(res);
                        A3_LOG("Acquire GPU [%s]\n", res ? "OK" : "NG");
                    }
                    wait = false;
                    device::instance()->bar1()->write(current, handle.second);
                    A3_LOG("timer thread fires FIRE %" PRIu32 " [%s]\n", current->id(), device::instance()->is_active() ? "ACTIVE" : "STOP");
                    will_be_sleep = false;
                }
            }
        }

#ifndef A3_BUSY_LOOP
out:
#endif
        if (will_be_sleep) {
            if (wait) {
                // A3_LOG("timer thread sleeps\n");
            }
#ifndef A3_BUSY_LOOP
            boost::this_thread::yield();
            boost::this_thread::sleep(wait_);
#endif
        }
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */