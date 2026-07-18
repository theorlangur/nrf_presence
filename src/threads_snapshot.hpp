#ifndef THREADS_SNAPSHOT_HPP_
#define THREADS_SNAPSHOT_HPP_

#include <iterator>
#include <zephyr/kernel.h>
#include <cstddef>
#include <cstdint>

namespace zephyr
{
    inline static constexpr uint32_t kSnapshotMagic = 0x54485244;//"THRD"
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

            void capture(k_thread *k, uintptr_t reg_fp) volatile
            {
                strncpy((char*)name, k->name, std::min(std::size(k->name), cfg.m_ThreadNameMaxLen));
                uintptr_t fp;
                if (reg_fp && _current == k)
                    fp = reg_fp;
                else
                    fp = k->callee_saved.*fp_ptr_reg;

                uintptr_t stack_start = k->stack_info.start;
                uintptr_t stack_end = k->stack_info.start + k->stack_info.size;
                int frames = 0;
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
            thread_t tasks[cfg.m_MaxTasks];

            void capture(uintptr_t reg_fp) volatile
            {
                int t_idx = 0;
                for (k_thread *current = _kernel.threads; current && (t_idx < cfg.m_MaxTasks); current = current->next_thread) {
                    tasks[t_idx++].capture(current, reg_fp);
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
