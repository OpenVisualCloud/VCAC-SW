#include "vca_pxe.h"

#include "helper_funcs.h"

#define VCA_PXE_ENABLE _IO('r', 1)
#define VCA_PXE_DISABLE _IO('r', 2)
#define VCA_PXE_QUERY _IOR('r', 3, __u32)

std::string stringify_pxe_state(enum vcapxe_state state) {
	switch (state) {
	case VCAPXE_STATE_INACTIVE:
		return "Inactive";
	case VCAPXE_STATE_ACTIVE:
		return "Active";
	case VCAPXE_STATE_RUNNING:
		return "In use";
	default:
		return "???";
	}
}

enum vcapxe_state get_pxe_state(filehandle_t pxe_dev_fd)
{
	__u32 buf;
	int err = ioctl(pxe_dev_fd, VCA_PXE_QUERY, &buf);

	if (err < 0) {
		LOG_ERROR("VCA_PXE_QUERY error: %s\n", strerror(errno));
		return static_cast<enum vcapxe_state>(-1);
	} else {
		return static_cast<enum vcapxe_state>(buf);
	}
}

std::string get_pxe_dev_name(int card_id, int cpu_id)
{
	return "vcapxe" + int_to_string(card_id) + int_to_string(cpu_id);
}

bool is_pxe_exactly_inactive(filehandle_t pxe_dev_fd)
{
	return get_pxe_state(pxe_dev_fd) == VCAPXE_STATE_INACTIVE;
}

bool is_pxe_exactly_active(filehandle_t pxe_dev_fd)
{
	return get_pxe_state(pxe_dev_fd) == VCAPXE_STATE_ACTIVE;
}

bool is_pxe_exactly_running(filehandle_t pxe_dev_fd)
{
	return get_pxe_state(pxe_dev_fd) == VCAPXE_STATE_RUNNING;
}

bool is_pxe_not_inactive(filehandle_t pxe_dev_fd)
{
	return get_pxe_state(pxe_dev_fd) != VCAPXE_STATE_INACTIVE;
}

bool pxe_enable(filehandle_t pxe_dev_fd)
{
	int err;

	err = ioctl(pxe_dev_fd, VCA_PXE_ENABLE);

	if (err < 0) {
		LOG_ERROR("VCA_PXE_ENABLE error: %s\n", strerror(errno));
		return false;
	} else {
		return true;
	}
}

bool pxe_disable(filehandle_t pxe_dev_fd)
{
	int err;

	err = ioctl(pxe_dev_fd, VCA_PXE_DISABLE);

	if (err < 0) {
		LOG_ERROR("VCA_PXE_DISABLE error: %s\n", strerror(errno));
		return false;
	} else {
		return true;
	}
}

