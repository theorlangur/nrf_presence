#ifndef PARTITION_BASIC_HPP_
#define PARTITION_BASIC_HPP_
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

namespace zephyr
{
    struct partition_basic_t
    {
        /* For use with flash map */
        const struct flash_area		*flash_area = nullptr;

        ///* For use with streaming flash */
        //struct stream_flash_ctx		stream_ctx;

        /* Checksum of data so far */
        uint16_t			checksum = 0;

        /* Error encountered */
        int				error = 0;

        partition_basic_t() = default;
        partition_basic_t(uint8_t partition_id)
        {
            open(partition_id);
        }

        ~partition_basic_t()
        {
            close();
        }

        operator bool() const { return error == 0; }

        int open(uint8_t partition_id)
        {
            error = flash_area_open(partition_id, &flash_area);
            if (error != 0)
                return error;
            error = 0;
            checksum = 0;
            return 0;
        }

        int erase()
        {
            if (!flash_area) return -1;
            error = flash_area_flatten(flash_area, 0,
                    flash_area->fa_size);
            return error;
        }

        void close()
        {
            if (flash_area)
                flash_area_close(flash_area);
            flash_area = nullptr;
            error = 0;
        }

        template<class T>
        int write(size_t off, T const& t)
        {
            if (!flash_area) return error;
            if (sizeof(T) > (flash_area->fa_size - off))
                return -1;
            error = flash_area_write(flash_area, off, (void *)&t, sizeof(T));
            return error;
        }

        template<class T>
        int read(size_t off, T &t)
        {
            if (!flash_area) return error;
            if (sizeof(T) > (flash_area->fa_size - off))
                return -1;
            error = flash_area_read(flash_area, off, (void *)&t, sizeof(T));
            return error;
        }
    };
}
#endif
