#ifndef __LIBMAPPING_CONNECT_H__
#define __LIBMAPPING_CONNECT_H__


#define libmapping_panic(var)


thread_t* libmapping_get_current_thread (void);
uint32_t libmapping_is_dynamic_mode (void);
uint32_t libmapping_initialized (void);

#endif
