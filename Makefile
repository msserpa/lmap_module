obj-m := lmap.o
lmap-objs := pid.o migrate.o memory.o map.o eagermap/mapping-greedy.o eagermap/topology.o eagermap/lib.o eagermap/graph.o main.o
DEVNAME := lmap

.PHONY: all clean

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	@echo "Module compiled!"

mod-install:
	insmod lmap.ko
	@echo "Module installed!"

mod-uninstall:
	- rmmod lmap

#	@if stat -t obj/* >/dev/null 2>&1; then mv -f obj/* . ; else mkdir -p obj; fi
#@mv -f *.o modules.order Module.symvers spcd.mod.c obj

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	

#	@rm -rf obj/