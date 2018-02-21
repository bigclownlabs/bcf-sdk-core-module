#include <bc_scheduler.h>
#include <bc_system.h>
#include <bc_error.h>

// TODO
#include <bc_gpio.h>

#define _BC_SCHEDULER_USE_MARKS 1

#define _BC_SCHEDULER_TASK_COUNT 64

#define _BC_SCHEDULER_INVALID_TASK_ID (_BC_SCHEDULER_TASK_COUNT)

typedef struct
{
    bc_tick_t tick_execution;

    void (*task)(void *);

    void *param;

} bc_scheduler_task_t;

typedef struct
{
    bc_tick_t tick_spin;

    bool extra_spin;

    int sleep_bypass_semaphore;

    bc_scheduler_task_id_t current_task_id;

    bc_scheduler_task_id_t max_task_id;

    bc_tick_t first_task_tick;

    bc_scheduler_task_t pool[_BC_SCHEDULER_TASK_COUNT];

} bc_scheduler_t;

static bc_scheduler_t _bc_scheduler;

extern void bc_system_error(void);

extern void _bc_scheduler_hook_set_interrupt_tick(bc_tick_t tick);

static inline bc_scheduler_task_id_t _bc_scheduler_task_get_first();

static void _bc_scheduler_plan_task(bc_scheduler_task_id_t task_id, bc_tick_t tick);

void bc_scheduler_init(void)
{
    memset(&_bc_scheduler, 0, sizeof(_bc_scheduler));
}

void bc_scheduler_run(void)
{
    static bc_scheduler_task_t *task;

    static bc_scheduler_task_id_t *task_id = &_bc_scheduler.current_task_id;

#if _BC_SCHEDULER_USE_MARKS
    bc_gpio_init(BC_GPIO_P0);
    bc_gpio_set_mode(BC_GPIO_P0, BC_GPIO_MODE_OUTPUT);

    bc_gpio_init(BC_GPIO_P3);
    bc_gpio_set_mode(BC_GPIO_P3, BC_GPIO_MODE_OUTPUT);

    bc_gpio_init(BC_GPIO_P2);
    bc_gpio_set_mode(BC_GPIO_P2, BC_GPIO_MODE_OUTPUT);
#endif

    while (1)
    {
#if _BC_SCHEDULER_USE_MARKS
        bc_gpio_set_output(BC_GPIO_P0, 1);
#endif

        _bc_scheduler.tick_spin = bc_system_tick_get();

        if (_bc_scheduler.tick_spin > _bc_scheduler.first_task_tick)
        {
            for (*task_id = 0; *task_id <= _bc_scheduler.max_task_id; (*task_id)++)
            {
                if (_bc_scheduler.pool[*task_id].task != NULL)
                {
                    task = &_bc_scheduler.pool[*task_id];

                    if (task->tick_execution <= _bc_scheduler.tick_spin)
                    {
#if _BC_SCHEDULER_USE_MARKS
        bc_gpio_set_output(BC_GPIO_P3, 1);
#endif

                        task->tick_execution = BC_TICK_INFINITY;

                        task->task(task->param);
                        
#if _BC_SCHEDULER_USE_MARKS
        bc_gpio_set_output(BC_GPIO_P3, 0);
#endif
                    }
                }
            }

            if (_bc_scheduler.extra_spin)
            {
                _bc_scheduler.extra_spin = false;

                continue;
            }

#if _BC_SCHEDULER_USE_MARKS
        bc_gpio_set_output(BC_GPIO_P2, 1);
#endif
            
            bc_tick_t tick_first = BC_TICK_INFINITY;

            for (bc_scheduler_task_id_t i = 0; i < _bc_scheduler.max_task_id; i++)
            {
                task = &_bc_scheduler.pool[i];

                if ((task->task != NULL) && (task->tick_execution < tick_first))
                {
                    tick_first = task->tick_execution;
                }
            }
            
            if (tick_first == 0)
            {
                continue;
            }

            _bc_scheduler_hook_set_interrupt_tick(tick_first);
            
#if _BC_SCHEDULER_USE_MARKS
        bc_gpio_set_output(BC_GPIO_P2, 0);
#endif
        }

#if _BC_SCHEDULER_USE_MARKS
        bc_gpio_set_output(BC_GPIO_P0, 0);
#endif

        if (_bc_scheduler.sleep_bypass_semaphore == 0)
        {
            bc_system_sleep();
        }

        continue;
    }
}

bc_scheduler_task_id_t bc_scheduler_register(void (*task)(void *), void *param, bc_tick_t tick)
{
    for (bc_scheduler_task_id_t i = 0; i < BC_SCHEDULER_MAX_TASKS; i++)
    {
        if (_bc_scheduler.pool[i].task == NULL)
        {
            _bc_scheduler.pool[i].task = task;

            _bc_scheduler.pool[i].param = param;

            if (_bc_scheduler.max_task_id < i)
            {
                _bc_scheduler.max_task_id = i;
            }

            bc_scheduler_plan_absolute(i, tick);

            return i;
        }
    }

    bc_system_error();

    // TODO Indicate no more tasks available
    for (;;);
}

void bc_scheduler_unregister(bc_scheduler_task_id_t task_id)
{
    bc_scheduler_task_t *task = &_bc_scheduler.pool[task_id];

    _bc_scheduler.pool[task_id].task = NULL;

    if (_bc_scheduler.max_task_id == task_id)
    {
        do
        {
            if (_bc_scheduler.max_task_id == 0)
            {
                break;
            }

            _bc_scheduler.max_task_id--;

        } while (_bc_scheduler.pool[_bc_scheduler.max_task_id].task == NULL);
    }

    // TODO
    // _bc_scheduler_reconfigure_timer_if_needed(task);
}

bc_scheduler_task_id_t bc_scheduler_get_current_task_id(void)
{
    return _bc_scheduler.current_task_id;
}

bc_tick_t bc_scheduler_get_spin_tick(void)
{
    return _bc_scheduler.tick_spin;
}

void bc_scheduler_disable_sleep(void)
{
    _bc_scheduler.sleep_bypass_semaphore++;
}

void bc_scheduler_enable_sleep(void)
{
    _bc_scheduler.sleep_bypass_semaphore--;
}

void bc_scheduler_plan_now(bc_scheduler_task_id_t task_id)
{
    _bc_scheduler_plan_task(task_id, 0);
}

void bc_scheduler_plan_absolute(bc_scheduler_task_id_t task_id, bc_tick_t tick)
{
    _bc_scheduler_plan_task(task_id, tick);
}

void bc_scheduler_plan_relative(bc_scheduler_task_id_t task_id, bc_tick_t tick)
{
    _bc_scheduler_plan_task(task_id, _bc_scheduler.tick_spin + tick);
}

void bc_scheduler_plan_from_now(bc_scheduler_task_id_t task_id, bc_tick_t tick)
{
    _bc_scheduler_plan_task(task_id, bc_tick_get() + tick);
}

void bc_scheduler_plan_current_now(void)
{
    _bc_scheduler_plan_task(_bc_scheduler.current_task_id, 0);
}

void bc_scheduler_plan_current_absolute(bc_tick_t tick)
{
    _bc_scheduler_plan_task(_bc_scheduler.current_task_id, tick);
}

void bc_scheduler_plan_current_relative(bc_tick_t tick)
{
    _bc_scheduler_plan_task(_bc_scheduler.current_task_id, _bc_scheduler.tick_spin + tick);
}

void bc_scheduler_plan_current_from_now(bc_tick_t tick)
{
    _bc_scheduler_plan_task(_bc_scheduler.current_task_id, bc_tick_get() + tick);
}

static inline bc_scheduler_task_id_t _bc_scheduler_task_get_first()
{
    bc_tick_t tick_first = BC_TICK_INFINITY;

    bc_scheduler_task_id_t task_first;

    bc_scheduler_task_t *task;

    bc_scheduler_task_id_t task_id;

    for (task_id = 0; task_id < _bc_scheduler.max_task_id; task_id++)
    {
        task = &_bc_scheduler.pool[task_id];

        if ((_bc_scheduler.pool[task_id].task != NULL) && (task->tick_execution < tick_first))
        {
            task_first = task_id;

            tick_first = task->tick_execution;
        }
    }

    return task_first;
}

static void _bc_scheduler_plan_task(bc_scheduler_task_id_t task_id, bc_tick_t tick)
{
    if (_bc_scheduler.first_task_tick > tick)
    {
        if (tick <= bc_system_tick_get())
        {
            _bc_scheduler.extra_spin = true;
        }

        _bc_scheduler.first_task_tick = tick;
    }

    _bc_scheduler.pool[task_id].tick_execution = tick;
}
