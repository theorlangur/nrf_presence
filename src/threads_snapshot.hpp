#ifndef THREADS_SNAPSHOT_HPP_
#define THREADS_SNAPSHOT_HPP_

#include <iterator>
#include <zephyr/kernel.h>
#include <cstddef>
#include <cstdint>

namespace zephyr
{
    inline static constexpr uint32_t kSnapshotMagic = 0x54485244;//"THRD"
    inline static constexpr uint32_t kSnapshotMagicNotReady = 0x54485243;//"THRC"
    inline static constexpr auto fp_ptr_reg = &_callee_saved::v4;
    struct snapshot_cfg_t
    {
        size_t m_MaxTasks = 10;
        size_t m_MaxFrames = 8;
        size_t m_ThreadNameMaxLen = 16;
    };

    template<snapshot_cfg_t cfg = {}>
    struct snapshot_factory_t
    {
        struct thread_t
        {
            char name[cfg.m_ThreadNameMaxLen];
            uintptr_t stack[cfg.m_MaxFrames];

            operator bool() const volatile
            {
                return name[0] || stack[0];
            }

            void capture(const k_thread *k, uintptr_t reg_fp = 0, int frames_offset = 0) volatile
            {
                const char *pSrc = k->name;
                volatile char *pDst = name;
                while(pSrc < std::end(k->name) && *pSrc && pDst < (name + cfg.m_ThreadNameMaxLen - 1))
                    *pDst++ = *pSrc++;
                *pDst = 0;
                uintptr_t fp = reg_fp;
                if (!fp)
                    fp = k->callee_saved.*fp_ptr_reg;

                uintptr_t stack_start = k->stack_info.start;
                uintptr_t stack_end = k->stack_info.start + k->stack_info.size;
                size_t frames = frames_offset;
                while(fp >= stack_start && fp < stack_end && (fp & 0x03) == 0 && frames < cfg.m_MaxFrames)
                {
                    uint32_t *fp_ptr = (uint32_t *)fp;
                    uint32_t next_fp = fp_ptr[0];
                    uint32_t return_addr = fp_ptr[1];

                    stack[frames++] = return_addr;
                    fp = next_fp;
                }
                if (frames < cfg.m_MaxFrames)
                    stack[frames] = 0;
            }
        };

        struct snapshot_t
        {
            uint32_t magic;
            uint16_t total_size;
            uint8_t  total_tasks;
            uint8_t  unused;
            thread_t tasks[cfg.m_MaxTasks];

            inline static uintptr_t reg_fp;
            inline static size_t pre_captured_frames_count;
            inline static uintptr_t pre_captured_frames[cfg.m_MaxFrames];

            static void on_thread_user_cb_t(const struct k_thread *thread,
                    void *user_data)
            {
                snapshot_t *s = (snapshot_t *)user_data;
                if(s->total_tasks < cfg.m_MaxTasks)
                {
                    if (thread == _current)
                    {
                        size_t frames = 0;
                        for(;frames < pre_captured_frames_count; ++frames)
                            s->tasks[s->total_tasks].stack[frames] = pre_captured_frames[frames];

                        s->tasks[s->total_tasks++].capture(thread, reg_fp, pre_captured_frames_count);
                    }else
                    {
                        s->tasks[s->total_tasks++].capture(thread);
                    }
                }
            }

            template<std::integral...T>
            void capture(uintptr_t _reg_fp, T... extra_frame) volatile
            {
                magic = kSnapshotMagicNotReady;
                total_size = sizeof(*this);
                total_tasks = 0;
                reg_fp = _reg_fp;
                static_assert(sizeof...(T) <= cfg.m_MaxFrames, "Too many extra frames to store");
                pre_captured_frames_count = sizeof...(T);
                [&]<size_t...I>(std::index_sequence<I...>)
                {
                    if constexpr (sizeof...(I) > 0)
                        ((pre_captured_frames[I] = extra_frame...[I]),...);
                }(std::make_index_sequence<sizeof...(T)>());
                k_thread *current = _kernel.threads;
                if (!current)
                    current = _current;
                k_thread_foreach_unlocked(on_thread_user_cb_t, (void*)this);
                if (total_tasks == 0)
                {
                    //fallback
                    for(;current && total_tasks < cfg.m_MaxTasks; current = current->next_thread)
                        on_thread_user_cb_t(current, (void*)this);
                }

                if (total_tasks < cfg.m_MaxTasks)
                {
                    tasks[total_tasks].name[0] = 0;
                    tasks[total_tasks].stack[0] = 0;
                }
                //at the end, as a final step
                magic = kSnapshotMagic;
            }

            void clear() volatile
            {
                magic = 0;
            }

            bool is_valid() volatile const
            {
                return magic == kSnapshotMagic;
            }
        };
    };
}

#endif
