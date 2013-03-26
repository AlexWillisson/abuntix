bin_PROGRAMS = mttf-add mttf-queue
mttf_add_SOURCES = mttf-add.c json.y json.h
mttf_queue_SOURCES = mttf-queue.c json.y json.h

install-exec-hook:
	sudo cp mttf-queue /etc/cron.hourly/mttf-queue

uninstall-hook:
	sudo rm mttf-queue /etc/cron.hourly/mttf-queue
