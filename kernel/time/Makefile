obj-y += timekeeping.o ntp.o clocksource.o jiffies.o timer_list.o timecompare.o
obj-y += timeconv.o posix-clock.o

ifndef CONFIG_RTC_INTF_ALARM
obj-y += alarmtimer.o
endif

obj-$(CONFIG_GENERIC_CLOCKEVENTS_BUILD)		+= clockevents.o
obj-$(CONFIG_GENERIC_CLOCKEVENTS)		+= tick-common.o
obj-$(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)	+= tick-broadcast.o
obj-$(CONFIG_TICK_ONESHOT)			+= tick-oneshot.o
obj-$(CONFIG_TICK_ONESHOT)			+= tick-sched.o
