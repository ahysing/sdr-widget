/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
** Host CLI for Henry Audio / audiophile-widget loudness control (bass boost
** flag via DG8SAQ vendor request or UAC2 SET_CUR). See docs/FIRMWARE_USAGE.md.
*/
const char usage[] = {
	"usage: ./henryctl [options] [values]\n"
	"         --help\n"
	"         -v = verbose = verbose mode\n"
	"         --bassboost 0|1 = set firmware bass boost flag.\n"
	"         --loudness 0|1 = set firmware loudness flag.\n"
}; 

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#if defined(__has_include)
#if __has_include("widget_control_usb_ids.h")
#include "widget_control_usb_ids.h"
#elif __has_include("src/widget_control_usb_ids.h")
#include "src/widget_control_usb_ids.h"
#else
#include "../src/widget_control_usb_ids.h"
#endif
#else
#include "src/widget_control_usb_ids.h"
#endif

/* DG8SAQ features API constants */
#ifndef FEATURE_DG8SAQ_COMMAND
#define FEATURE_DG8SAQ_COMMAND        0x71
#define FEATURE_DG8SAQ_SET_NVRAM      3
#define FEATURE_DG8SAQ_GET_NVRAM      4
#define FEATURE_DG8SAQ_SET_RAM        5
#define FEATURE_DG8SAQ_GET_RAM        6
#define FEATURE_DG8SAQ_GET_INDEX_NAME 7
#define FEATURE_DG8SAQ_GET_VALUE_NAME 8
#define FEATURE_DG8SAQ_GET_DEFAULT    9
#endif

int verbose = 0;
static int require_features = 1;

/* UAC2 Audio Control class request constants (see usb_audio.h). */
#define AUDIO_CS_REQUEST_CUR                0x01
#define AUDIO_FU_CONTROL_CS_BASS_BOOST      0x09
#define AUDIO_FU_CONTROL_CS_LOUDNESS          0x0A
#define AUDIO_INTERFACE                     0x01
#define AUDIO_INTERFACE_SUBCLASS_AUDIOCONTROL 0x01
#define AUDIO_INTERFACE_IP_VERSION_02_00    0x20
#define SPK_FEATURE_UNIT_ID                 0x14
#define DG8SAQ_SET_BASS_BOOST               0x72
#define DG8SAQ_SET_LOUDNESS                 0x73

typedef struct {
	const char *label;
	unsigned char dg8saq_request;
	unsigned char uac2_control_cs;
} henryctl_bool_feature_t;

static const henryctl_bool_feature_t henryctl_feature_bass_boost = {
	"Bass boost",
	DG8SAQ_SET_BASS_BOOST,
	AUDIO_FU_CONTROL_CS_BASS_BOOST,
};

static const henryctl_bool_feature_t henryctl_feature_loudness = {
	"Loudness",
	DG8SAQ_SET_LOUDNESS,
	AUDIO_FU_CONTROL_CS_LOUDNESS,
};

/*
** features
*/
#define true_feature_major_index 0
#define true_feature_minor_index 1
int true_feature_end_index;
int true_feature_end_values;

// features_t features_default = { FEATURES_DEFAULT };
uint8_t *true_features_default;
//features_t features_nvram;
uint8_t *true_features_nvram;
//features_t features_mem;
uint8_t *true_features_mem;
//char *feature_index_names[] = { FEATURE_INDEX_NAMES };
char **true_feature_index_names;
//char *feature_value_names[] = { FEATURE_VALUE_NAMES };
char **true_feature_value_names;
//int feature_first_value[feature_end_index];
int *feature_first_value;
// int feature_last_value[feature_end_index];
int *feature_last_value;

int finish(int return_value);

void feature_first_and_last_init(void) {
	int i, j;
	feature_first_value[true_feature_major_index] = -1;
	feature_first_value[true_feature_minor_index] = -1;
	feature_last_value[true_feature_major_index] = -1;
	feature_last_value[true_feature_minor_index] = -1;
	if (true_feature_minor_index+1 < true_feature_end_index) { 
		feature_first_value[true_feature_minor_index+1] = 0;
		for (i = true_feature_minor_index+1; i < true_feature_end_index; i += 1) {
			for (j = feature_first_value[i]; j < true_feature_end_values && strcmp(true_feature_value_names[j], "end") != 0; j += 1);
			feature_last_value[i] = j-1;
			feature_first_value[i+1] = j+1;
		}
	}
}

int first_value(int index) {
	if (index > true_feature_minor_index && index < true_feature_end_index)
		return feature_first_value[index];
	return -1;
}

int last_value(int index) {
	if (index > true_feature_minor_index && index < true_feature_end_index)
		return feature_last_value[index];
	return -1;
}

int find_feature_value(int index, char *value) {
	int i;
	for (i = first_value(index); i <= last_value(index); i += 1)
		if (strcmp(true_feature_value_names[i], value) == 0)
			return i;
	return -1;
}

/*
** usb
*/
#define REQDIR_HOSTTODEVICE		(0 << 7)
#define REQDIR_DEVICETOHOST		(1 << 7)
#define REQTYPE_STANDARD		(0 << 5)
#define REQTYPE_CLASS			(1 << 5)
#define REQTYPE_VENDOR			(2 << 5)
#define REQREC_DEVICE			(0 << 0)
#define REQREC_INTERFACE		(1 << 0)
#define REQREC_ENDPOINT			(2 << 0)
#define REQREC_OTHER			(3 << 0)

#define WIDGET_RESET			0x0f

char *usb_serial_id = NULL;
libusb_device_handle *usb_handle;
char *usb_device = "none";
char usb_data[1024];
unsigned int usb_timeout = 2000;


char *error_string(int err) {
	switch (err) {
	case LIBUSB_SUCCESS: return "Success (no error).";
	case LIBUSB_ERROR_IO: return "Input/output error.";
	case LIBUSB_ERROR_INVALID_PARAM: return "Invalid parameter.";
	case LIBUSB_ERROR_ACCESS: return "Access denied (insufficient permissions).";
	case LIBUSB_ERROR_NO_DEVICE: return "No such device (it may have been disconnected).";
	case LIBUSB_ERROR_NOT_FOUND: return "Entity not found.";
	case LIBUSB_ERROR_BUSY: return "Resource busy.";
	case LIBUSB_ERROR_TIMEOUT: return "Operation timed out.";
	case LIBUSB_ERROR_OVERFLOW: return "Overflow.";
	case LIBUSB_ERROR_PIPE: return "Pipe error.";
	case LIBUSB_ERROR_INTERRUPTED: return "System call interrupted (perhaps due to signal).";
	case LIBUSB_ERROR_NO_MEM: return "Insufficient memory.";
	case LIBUSB_ERROR_NOT_SUPPORTED: return "Operation not supported or unimplemented on this platform.";
	case LIBUSB_ERROR_OTHER: return "Other error.";
	default: {
		static char buff[256];
		sprintf(buff, "undefined error %d", err);
		return buff;
	}
	}
}

int device_to_host_handle(libusb_device_handle *h, unsigned char request, unsigned short value, unsigned short index, unsigned short length) {
	return libusb_control_transfer(h,
		(REQDIR_DEVICETOHOST | REQTYPE_VENDOR | REQREC_DEVICE),
		request, value, index, usb_data, length, usb_timeout);
}

int host_to_device_handle(libusb_device_handle *h, unsigned char request, unsigned short value, unsigned short index, unsigned char *data, unsigned short length) {
	return libusb_control_transfer(h,
		(REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE),
		request, value, index, (unsigned char *)data, length, usb_timeout);
}

static int host_to_device_vendor_handle(libusb_device_handle *h,
	unsigned char request, unsigned short value, unsigned short index,
	unsigned char *data, unsigned short length)
{
	return libusb_control_transfer(h,
		(REQDIR_HOSTTODEVICE | REQTYPE_VENDOR | REQREC_DEVICE),
		request, value, index, data, length, usb_timeout);
}
	
int device_to_host(unsigned char request, unsigned short value, unsigned short index, unsigned short length) {
	return device_to_host_handle(usb_handle, request, value, index, length);
}

static int read_device_serial(libusb_device_handle *h, uint8_t i_serial,
	unsigned char *serial_id, size_t serial_size)
{
	int status;

	if (i_serial == 0)
		return 0;

	status = libusb_get_string_descriptor_ascii(h, i_serial, serial_id,
		(int)serial_size);
	if (status > 0)
		return status;

	if (libusb_claim_interface(h, 0) != 0)
		return status;

	status = libusb_get_string_descriptor_ascii(h, i_serial, serial_id,
		(int)serial_size);
	libusb_release_interface(h, 0);
	return status;
}

static int probe_features_api(libusb_device_handle *h)
{
	int status;
	int iface;

	for (iface = 0; iface < 8; iface += 1) {
		if (libusb_claim_interface(h, iface) != 0)
			continue;

		status = device_to_host_handle(h, FEATURE_DG8SAQ_COMMAND,
			FEATURE_DG8SAQ_GET_NVRAM, true_feature_major_index, 8);
		libusb_release_interface(h, iface);

		if (status == 1 && (usb_data[0] & 0xff) != 0xff)
			return 1;
	}
	return 0;
}

static int claim_interface_optional(libusb_device_handle *h, int iface)
{
	libusb_set_auto_detach_kernel_driver(h, 1);
	return libusb_claim_interface(h, iface);
}

static int find_dg8saq_config_interface(libusb_device_handle *h);
static int claim_dg8saq_config_interface(libusb_device_handle *h, int *claimed_out);
static int find_audio_control_interface(libusb_device_handle *h);
static int get_bool_feature_vendor(libusb_device_handle *h,
	unsigned char dg8saq_request);
static int set_bool_feature_vendor(libusb_device_handle *h,
	unsigned char dg8saq_request, int enabled);
static int set_bool_feature_uac2(libusb_device_handle *h,
	unsigned char uac2_control_cs, int enabled);
static int set_bool_feature(const henryctl_bool_feature_t *feature, int enabled);
	

// this needs modification to handle the -u usb_serial_id option
// must get the total list of devices and query matches for serial id
libusb_device_handle *find_device(int list_all) {
	libusb_device **list;
	ssize_t n_items = libusb_get_device_list(NULL, &list);
	int i;

	for (i = 0; i < n_items; i += 1) {
		libusb_device *d = list[i];
		struct libusb_device_descriptor desc;
		libusb_device_handle *h;
		unsigned char serial_id[1024];
		int status;

		if (libusb_get_device_descriptor(d, &desc) != 0)
			continue;

		if (!widget_usb_id_matches(desc.idVendor, desc.idProduct))
			continue;

		if ((status = libusb_open(d, &h)) != 0) {
			if (verbose)
				fprintf(stderr,
					"find_device: libusb_open(%04x:%04x) failed: %s\n",
					desc.idVendor, desc.idProduct, error_string(status));
			continue;
		}

		status = read_device_serial(h, desc.iSerialNumber, serial_id,
			sizeof(serial_id));
		if (status <= 0) {
			if (verbose)
				fprintf(stderr,
					"find_device: %04x:%04x serial read failed: %s\n",
					desc.idVendor, desc.idProduct,
					status == 0 ? "empty" : error_string(status));
			libusb_close(h);
			continue;
		}
		serial_id[status] = 0;

		if (usb_serial_id != NULL && strcmp((char *)serial_id, usb_serial_id) != 0) {
			libusb_close(h);
			continue;
		}

		if (list_all) {
			fprintf(stdout, "%04x:%04x %s\n",
				desc.idVendor, desc.idProduct, serial_id);
			if (usb_handle == NULL)
				usb_handle = h;
			else
				libusb_close(h);
			continue;
		}

		if (require_features && !probe_features_api(h)) {
			if (verbose)
				fprintf(stderr,
					"find_device: %04x:%04x has no features API\n",
					desc.idVendor, desc.idProduct);
			libusb_close(h);
			continue;
		}

		usb_handle = h;
		break;
	}
	libusb_free_device_list(list, 1);
	return usb_handle;
}

void setup() {
	libusb_init(NULL);
	usb_handle = find_device(0);
	if (usb_handle == NULL) {
		fprintf(stderr, "henryctl: failed to find device");
		if (!verbose)
			fprintf(stderr, " (try -v for details)");
		fprintf(stderr, "\n");
		libusb_exit(NULL);
		exit(1);
	}
	if ( verbose )
		fprintf(stdout, "henryctl: opened %s device\n", usb_device);
	{
		int cfg_if = find_dg8saq_config_interface(usb_handle);
		if (cfg_if < 0) {
			fprintf(stderr,
				"henryctl: no DG8SAQ config interface (flash firmware with FEATURE_CFG_INTERFACE)\n");
			exit(finish(1));
		}
		if (libusb_claim_interface(usb_handle, cfg_if) != 0) {
			fprintf(stderr,
				"henryctl: failed to claim config interface %d\n",
				cfg_if);
			exit(finish(1));
		}
	}
	{
		// read out the widget's major and minor version numbers
		int i, j, res = device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_NVRAM, true_feature_major_index, 8);
		if (res == 1)
			true_feature_end_index = usb_data[0];
		else {
			fprintf(stderr, "unable to read true feature_end_index\n");
			exit(finish(1));
		}
		res = device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_NVRAM, true_feature_minor_index, 8);
		if (res == 1)
			true_feature_end_values = usb_data[0];
		else {
			fprintf(stderr, "unable to read true feature_end_values\n");
			exit(finish(1));
		}
		// allocate arrays for feature values and names
		true_features_default = (uint8_t *)calloc(true_feature_end_index, sizeof(uint8_t));
		true_features_nvram = (uint8_t *)calloc(true_feature_end_index, sizeof(uint8_t));
		true_features_mem = (uint8_t *)calloc(true_feature_end_index, sizeof(uint8_t));
		true_feature_index_names = (char **)calloc(true_feature_end_index, sizeof(char *));
		true_feature_value_names = (char **)calloc(true_feature_end_values, sizeof(char *));
		feature_first_value = (int *)calloc(true_feature_end_index, sizeof(int));
		feature_last_value = (int *)calloc(true_feature_end_index, sizeof(int));
		if (true_features_default == NULL ||
			true_features_nvram == NULL ||
			true_features_mem == NULL ||
			true_feature_index_names == NULL ||
			true_feature_value_names == NULL ||
			feature_first_value == NULL ||
			feature_last_value == NULL) {
			fprintf(stderr, "unable to allocate memory for feature data\n");
			exit(finish(1));
		}
		// read out the widget's index and value names
		for (i = true_feature_major_index; i < true_feature_end_index; i += 1) {
			res = device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_INDEX_NAME, i, sizeof(usb_data));
			true_feature_index_names[i] = calloc(res+1, sizeof(char));
			if (true_feature_index_names[i] == NULL) {
				fprintf(stderr, "unable to allocate memory for feature data\n");
				exit(finish(1));
			}
			for (j = 0; j < res; j += 1) true_feature_index_names[i][res-j-1] = usb_data[j];
			// strncpy(true_feature_index_names[i], usb_data, res);
			true_feature_index_names[i][res] = 0;
			// fprintf(stderr, "retrieved '%s' for index name of %d\n", true_feature_index_names[i], i);
		}
		for (i = 0; i < true_feature_end_values; i += 1) {
			res = device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_VALUE_NAME, i, sizeof(usb_data));
			true_feature_value_names[i] = calloc(res+1, sizeof(char));
			if (true_feature_value_names[i] == NULL) {
				fprintf(stderr, "unable to allocate memory for feature data\n");
				exit(finish(1));
			}
			for (j = 0; j < res; j += 1) true_feature_value_names[i][res-j-1] = usb_data[j];
			// strncpy(true_feature_value_names[i], usb_data, res);
			true_feature_value_names[i][res] = 0;
			// fprintf(stderr, "retrieved '%s' for value name of %d\n", true_feature_value_names[i], i);
		}
		// set up index to value mapping
		feature_first_and_last_init();
		// fetch default values from image
		for (i = 0; i < true_feature_end_index; i += 1) {
			res = device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_DEFAULT, i, 8);
			if (res == 1)
				true_features_default[i] = usb_data[0];
			else {
				fprintf(stderr, "unable to read true features_default %d\n", i);
				exit(finish(1));
			}
		}
	}
}

int finish(int return_value) {
	if (usb_handle != NULL) {
		libusb_release_interface(usb_handle, 0);
		libusb_close(usb_handle);
	}
	libusb_exit(NULL);
	return return_value;
}

int get_all_devices() {
	libusb_init(NULL);
	usb_handle = find_device(1);
	return finish(0);
}

/*
** functions
*/
void print_all_features(uint8_t *fp) {
	int j;
	fprintf(stdout, "%d %d", fp[true_feature_major_index], fp[true_feature_minor_index]);
	for (j = true_feature_minor_index+1; j < true_feature_end_index; j += 1) {
		fprintf(stdout, " %s", true_feature_value_names[fp[j]]);
	}
	fprintf(stdout, "\n");
}

void list_all_features() {
	int j, k;
	setup();
	for (j = 0; j < true_feature_end_index; j += 1) {
		switch (j) {
		case true_feature_major_index:
		case true_feature_minor_index:
			fprintf(stdout, "%s %d\n", true_feature_index_names[j], true_features_default[j]);
			continue;
		default:
			fprintf(stdout, "%s = ", true_feature_index_names[j]);
			for (k = first_value(j); k <= last_value(j); k += 1)
				fprintf(stdout, " %s", true_feature_value_names[k]);
			fprintf(stdout, " \n");
		}
	}
}

int get_nvram() {
	int i;
	setup();
	for (i = true_feature_major_index; i < true_feature_end_index; i += 1) {
		int res = device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_NVRAM, i, 8);
		if (res == 1)
			true_features_nvram[i] = usb_data[0];
		else {
			fprintf(stderr, "henryctl: device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_NVRAM, %d, 0) returned %s?\n", i, error_string(res));
			exit(finish(1));
		}
	}
	print_all_features(true_features_nvram);
	return finish(0);
}

int get_mem() {
	int i;
	setup();
	for (i = true_feature_major_index; i < true_feature_end_index; i += 1) {
		int res = device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_RAM, i, 8);
		if (res == 1)
			true_features_mem[i] = usb_data[0];
		else {
			fprintf(stderr, "henryctl: device_to_host(FEATURE_DG8SAQ_COMMAND, FEATURE_DG8SAQ_GET_RAM, %d, 0) returned %s?\n", i, error_string(res));
			exit(finish(1));
		}
	}
	print_all_features(true_features_mem);
	return finish(0);
}

int get_default() {
	setup();
	print_all_features(true_features_default);
	return finish(0);
}

int reset_widget() {
	setup();
	int res = device_to_host(WIDGET_RESET, 0, 0, 8);
	if (res != 1) {
		fprintf(stderr, "henryctl: device_to_host(WIDGET_RESET, 0, 0, 8) returned %s?\n", error_string(res));
		exit(finish(1));
	}
    return finish(0);
}

static int find_dg8saq_config_interface(libusb_device_handle *h)
{
	libusb_device *dev = libusb_get_device(h);
	struct libusb_config_descriptor *config;
	int cfg_if = -1;
	int cfg_status;
	int i;
	int j;

	cfg_status = libusb_get_config_descriptor(dev, 0, &config);
	if (cfg_status != 0)
		return -1;

	for (i = 0; i < config->bNumInterfaces; i += 1) {
		const struct libusb_interface *intf = &config->interface[i];
		for (j = 0; j < intf->num_altsetting; j += 1) {
			const struct libusb_interface_descriptor *desc =
				&intf->altsetting[j];
			if (desc->bInterfaceClass == 0
				&& desc->bInterfaceSubClass == 0
				&& desc->bNumEndpoints == 0) {
				cfg_if = desc->bInterfaceNumber;
				break;
			}
		}
		if (cfg_if >= 0)
			break;
	}

	libusb_free_config_descriptor(config);
	return cfg_if;
}

static void dump_usb_interfaces(libusb_device_handle *h)
{
	libusb_device *dev = libusb_get_device(h);
	struct libusb_config_descriptor *config;
	int i;
	int j;

	if (!verbose)
		return;
	if (libusb_get_config_descriptor(dev, 0, &config) != 0)
		return;

	fprintf(stderr, "henryctl: USB configuration interfaces:\n");
	for (i = 0; i < config->bNumInterfaces; i += 1) {
		const struct libusb_interface *intf = &config->interface[i];
		for (j = 0; j < intf->num_altsetting; j += 1) {
			const struct libusb_interface_descriptor *desc =
				&intf->altsetting[j];
			fprintf(stderr,
				"  interface %u alt %u: class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u\n",
				desc->bInterfaceNumber, desc->bAlternateSetting,
				desc->bInterfaceClass, desc->bInterfaceSubClass,
				desc->bInterfaceProtocol, desc->bNumEndpoints);
		}
	}
	libusb_free_config_descriptor(config);
}

static int find_audio_control_interface(libusb_device_handle *h)
{
	libusb_device *dev = libusb_get_device(h);
	struct libusb_config_descriptor *config;
	int audio_if = -1;
	int cfg_status;
	int i;
	int j;

	cfg_status = libusb_get_config_descriptor(dev, 0, &config);
	if (cfg_status != 0)
		return -1;

	for (i = 0; i < config->bNumInterfaces; i += 1) {
		const struct libusb_interface *intf = &config->interface[i];
		for (j = 0; j < intf->num_altsetting; j += 1) {
			const struct libusb_interface_descriptor *desc =
				&intf->altsetting[j];
			if (desc->bInterfaceClass == AUDIO_INTERFACE
				&& desc->bInterfaceSubClass
					== AUDIO_INTERFACE_SUBCLASS_AUDIOCONTROL
				&& desc->bInterfaceProtocol
					== AUDIO_INTERFACE_IP_VERSION_02_00) {
				audio_if = desc->bInterfaceNumber;
				break;
			}
		}
		if (audio_if >= 0)
			break;
	}

	libusb_free_config_descriptor(config);
	return audio_if;
}

static int open_usb_device(void)
{
	libusb_init(NULL);
	usb_handle = find_device(0);
	if (usb_handle == NULL) {
		fprintf(stderr, "henryctl: failed to find device");
		if (!verbose)
			fprintf(stderr, " (try -v for details)");
		fprintf(stderr, "\n");
		libusb_exit(NULL);
		return 1;
	}
	return 0;
}

static int close_usb_device(int return_value)
{
	if (usb_handle != NULL) {
		libusb_close(usb_handle);
		usb_handle = NULL;
	}
	libusb_exit(NULL);
	return return_value;
}

static int claim_dg8saq_config_interface(libusb_device_handle *h, int *claimed_out)
{
	int cfg_if;
	int claim_res;

	if (claimed_out != NULL)
		*claimed_out = 0;

	cfg_if = find_dg8saq_config_interface(h);
	if (cfg_if < 0)
		return -1;

	claim_res = claim_interface_optional(h, cfg_if);
	if (claim_res != 0) {
		if (verbose)
			fprintf(stderr,
				"henryctl: config interface %d found but claim failed: %s\n",
				cfg_if, error_string(claim_res));
		return cfg_if;
	}

	if (claimed_out != NULL)
		*claimed_out = 1;
	return cfg_if;
}

static int get_bool_feature_vendor(libusb_device_handle *h,
	unsigned char dg8saq_request)
{
	int res;

	res = device_to_host_handle(h, dg8saq_request, 0, 0, 1);
	if (res != 1)
		return -1;
	if (usb_data[0] != 0 && usb_data[0] != 1)
		return -2;
	return usb_data[0] ? 1 : 0;
}

static int set_bool_feature_vendor(libusb_device_handle *h,
	unsigned char dg8saq_request, int enabled)
{
	unsigned char value = (unsigned char)enabled;
	int res;
	int state;

	res = host_to_device_vendor_handle(h, dg8saq_request, 0, 0, &value, 1);
	if (res != 1)
		return res;

	state = get_bool_feature_vendor(h, dg8saq_request);
	if (state < 0)
		return LIBUSB_ERROR_NOT_SUPPORTED;
	if (state == enabled)
		return 0;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

static int set_bool_feature_uac2(libusb_device_handle *h,
	unsigned char uac2_control_cs, int enabled)
{
	int audio_if;
	unsigned char value;
	unsigned short wIndex;
	int res;
	int claimed = 0;

	audio_if = find_audio_control_interface(h);
	if (audio_if < 0)
		return LIBUSB_ERROR_NOT_FOUND;

	if (claim_interface_optional(h, audio_if) == 0)
		claimed = 1;
	else if (verbose)
		fprintf(stderr,
			"henryctl: could not claim audio interface %d, trying SET_CUR anyway\n",
			audio_if);

	value = (unsigned char)enabled;
	wIndex = (unsigned short)((SPK_FEATURE_UNIT_ID << 8) | (audio_if & 0xff));
	res = host_to_device_handle(h, AUDIO_CS_REQUEST_CUR,
		(unsigned short)((uac2_control_cs << 8) | 0x00),
		wIndex, &value, 1);

	if (claimed)
		libusb_release_interface(h, audio_if);

	if (res != 1)
		return res;
	return 0;
}

static void henryctl_print_vendor_failure(const henryctl_bool_feature_t *feature,
	int res, int cfg_if, int cfg_claimed)
{
	if (cfg_if < 0) {
		fprintf(stderr,
			"henryctl: vendor %s failed: %s\n",
			feature->label, error_string(res));
	} else if (!cfg_claimed) {
		fprintf(stderr,
			"henryctl: vendor %s failed: %s (could not claim config interface %d; see docs/FIRMWARE_USAGE.md USBView section)\n",
			feature->label, error_string(res), cfg_if);
	} else if (verbose) {
		fprintf(stderr,
			"henryctl: vendor %s failed (%s), trying UAC2 SET_CUR\n",
			feature->label, error_string(res));
	} else {
		fprintf(stderr,
			"henryctl: vendor %s failed: %s (flash firmware with DG8SAQ 0x%02x)\n",
			feature->label, error_string(res), feature->dg8saq_request);
	}
}

static void henryctl_print_uac2_failure(const henryctl_bool_feature_t *feature,
	int res, int cfg_if, int cfg_claimed)
{
	fprintf(stderr,
		"henryctl: %s SET_CUR failed: %s\n",
		feature->label, error_string(res));
	if (cfg_if < 0)
		fprintf(stderr,
			"henryctl: hint: flash firmware with FEATURE_CFG_INTERFACE and DG8SAQ 0x%02x\n",
			feature->dg8saq_request);
	else if (!cfg_claimed)
		fprintf(stderr,
			"henryctl: hint: use Zadig WinUSB on config interface %d only (keep usbaudio on audio interfaces)\n",
			cfg_if);
	else
		fprintf(stderr,
			"henryctl: hint: flash firmware with DG8SAQ 0x%02x handler\n",
			feature->dg8saq_request);
}

static int set_bool_feature(const henryctl_bool_feature_t *feature, int enabled)
{
	int res;
	int cfg_if = -1;
	int cfg_claimed = 0;
	const char *enabled_text = enabled ? "enabled" : "disabled";

	if (enabled != 0 && enabled != 1) {
		fprintf(stderr, "henryctl: invalid %s value %d, use 0 or 1\n",
			feature->label, enabled);
		return 1;
	}

	require_features = 0;
	if (open_usb_device() != 0)
		return 1;

	cfg_if = claim_dg8saq_config_interface(usb_handle, &cfg_claimed);
	if (cfg_if < 0) {
		fprintf(stderr,
			"henryctl: no DG8SAQ config interface in USB descriptors\n");
		if (verbose)
			dump_usb_interfaces(usb_handle);
		fprintf(stderr,
			"henryctl: flash firmware built with FEATURE_CFG_INTERFACE\n");
	} else if (cfg_claimed) {
		if (verbose)
			fprintf(stderr,
				"henryctl: claimed config interface %d\n", cfg_if);
	} else if (verbose) {
		fprintf(stderr,
			"henryctl: config interface %d present, trying vendor request without claim\n",
			cfg_if);
	}

	res = set_bool_feature_vendor(usb_handle, feature->dg8saq_request, enabled);
	if (cfg_claimed)
		libusb_release_interface(usb_handle, cfg_if);

	if (res == 0) {
		if (verbose)
			fprintf(stderr,
				"henryctl: %s set via vendor request 0x%02x\n",
				feature->label, feature->dg8saq_request);
		fprintf(stdout, "%s %s\n", feature->label, enabled_text);
		return close_usb_device(0);
	}

	henryctl_print_vendor_failure(feature, res, cfg_if, cfg_claimed);

	res = set_bool_feature_uac2(usb_handle, feature->uac2_control_cs, enabled);
	if (res != 0) {
		henryctl_print_uac2_failure(feature, res, cfg_if, cfg_claimed);
		return close_usb_device(1);
	}

	fprintf(stdout, "%s %s\n", feature->label, enabled_text);
	return close_usb_device(0);
}

int set_bass_boost(int enabled)
{
	return set_bool_feature(&henryctl_feature_bass_boost, enabled);
}

int set_loudness(int enabled)
{
	return set_bool_feature(&henryctl_feature_loudness, enabled);
}

int main(int argc, char *argv[]) {
	int i;
	for (i = 1; i < argc; i += 1) {
		if (strcmp(argv[i], "-h") == 0 ||
			strcmp(argv[i], "--help") == 0) {
			fprintf(stdout, "%s", usage);
			return 0;
		}

		if (strcmp(argv[i], "-v") == 0) { // be verbose
			verbose = 1;
			continue;
		}

	}

	for (i = 1; i < argc; i += 1) {
		if (strcmp(argv[i], "--bassboost") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "henryctl: value required for --bassboost (0 or 1)\n");
				return 1;
			}

			exit(set_bass_boost(atoi(argv[++i])));
		}

		if (strcmp(argv[i], "--loudness") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "henryctl: value required for --loudness (0 or 1)\n");
				return 1;
			}

			exit(set_loudness(atoi(argv[++i])));
		}
	}
	
	fprintf(stderr, "%s", usage);
	return 1;
}
