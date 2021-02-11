/*
 * Copyright (c) 2020, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file nrf_802154_buffer_allocator.c
 * @brief Buffer allocation for 802.15.4 receptions and transmissions.
 */

#include "nrf_802154_buffer_allocator.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if CONFIG_NRF_802154_SER_BUFFER_ALLOCATOR_THREAD_SAFE
#include "nrf.h"
#endif

#if CONFIG_NRF_802154_SER_BUFFER_ALLOCATOR_THREAD_SAFE
static volatile uint32_t m_critical_section; ///< Current state of critical section.
#endif

/** @brief Enters a critical section. */
static void critical_section_enter(void)
{
#if CONFIG_NRF_802154_SER_BUFFER_ALLOCATOR_THREAD_SAFE
    m_critical_section = __get_PRIMASK();
    __disable_irq();
#endif
}

/** @brief Exits a critical section. */
static void critical_section_exit(void)
{
#if CONFIG_NRF_802154_SER_BUFFER_ALLOCATOR_THREAD_SAFE
    __set_PRIMASK(m_critical_section);
#endif
}

static uint8_t * buffer_alloc(nrf_802154_buffer_t * p_buffer_pool, size_t buffer_pool_len)
{
    nrf_802154_buffer_t * p_buffer = NULL;
    bool                  success  = false;
    bool                  retry    = false;

    do
    {
        retry = false;

        // Iterate over the buffer pool to search for a free buffer
        for (uint32_t i = 0; i < buffer_pool_len; i++)
        {
            p_buffer = &p_buffer_pool[i];

            if (!p_buffer->taken)
            {
                // Free buffer detected. Enter critical section to take it
                critical_section_enter();

                if (p_buffer->taken)
                {
                    // The allocation was preempted and the buffer was taken by higher priority
                    // Reiterate over the buffer pool and search for a free buffer again
                    retry = true;
                }
                else
                {
                    // The allocation can be performed safely
                    p_buffer->taken = true;
                    success = true;
                }

                critical_section_exit();

                break;
            }
        }
    } while (retry);

    return success ? p_buffer->data : NULL;
}

static void buffer_free(nrf_802154_buffer_t * p_buffer_to_free,
                        nrf_802154_buffer_t * p_buffer_pool,
                        size_t                buffer_pool_len)
{
    size_t idx =
        ((uintptr_t)p_buffer_to_free - (uintptr_t)p_buffer_pool) / sizeof(nrf_802154_buffer_t);

    assert(idx < buffer_pool_len);

    critical_section_enter();

    p_buffer_pool[idx].taken = false;

    critical_section_exit();
}

void nrf_802154_buffer_allocator_init(nrf_802154_buffer_allocator_t * p_obj,
                                      void                          * p_memory,
                                      size_t                          memsize)
{
    size_t capacity = memsize / sizeof(nrf_802154_buffer_t);

    assert((capacity == 0U) || ((capacity != 0U) && (p_memory != NULL)));

    p_obj->p_memory = p_memory;
    p_obj->capacity = capacity;

    nrf_802154_buffer_t * p_buffer = (nrf_802154_buffer_t *)p_obj->p_memory;

    for (size_t i = 0; i < p_obj->capacity; i++)
    {
        p_buffer[i].taken = false;
    }
}

void * nrf_802154_buffer_allocator_alloc(const nrf_802154_buffer_allocator_t * p_obj)
{
    return buffer_alloc((nrf_802154_buffer_t *)p_obj->p_memory, p_obj->capacity);
}

void nrf_802154_buffer_allocator_free(const nrf_802154_buffer_allocator_t * p_obj,
                                      void                                * p_buffer)
{
    buffer_free(p_buffer, (nrf_802154_buffer_t *)p_obj->p_memory, p_obj->capacity);
}
