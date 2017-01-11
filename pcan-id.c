/*
 * pcan-id
 * -------
 * 
 * CLI to modify serial number and device id of Peak CAN USB devices
 *
 * 
 * Copyright (C) 2016 Mario Kicherer (dev@kicherer.org)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <endian.h>

#include <libusb.h>


#define USB_TIMEOUT_MS 2000

struct pcan_type {
	char *name;
	uint16_t vendor_id;
	uint16_t product_id;
};

struct pcan_type pcan_types[] = {
	{
		.name = "PCAN-USB",
		.vendor_id = 0x0c72,
		.product_id = 0x000c,
	},
	
	{0},
};

struct pcan_ctx {
	struct libusb_context *usb_ctx;
	struct libusb_config_descriptor *config_descr;
	struct libusb_device_descriptor dev_descr;
	
	libusb_device *device;
	libusb_device_handle *dev_handle;
	
	struct pcan_type *pcan_type;
};


static int browse_devices(struct pcan_ctx *ctx, uint8_t device_idx, libusb_device **device, libusb_device_handle **dev_handle, 
						  struct pcan_type **pcan_type, uint8_t list_devices)
{
	struct pcan_type *p;
	int r, i, c;
	libusb_device **devices;
	char supported;
	
	
	r = libusb_get_device_list(ctx->usb_ctx, &devices);
	if (r < 0) {
		fprintf(stderr, "error retrieving list of devices: %s\n", libusb_strerror(r));
		return 1;
	}
	
	*device = 0;
	*pcan_type = 0;
	*dev_handle = 0;
	c = 0;
	i = 0;
	while (devices[c]) {
		r = libusb_get_device_descriptor(devices[c], &ctx->dev_descr);
		if (r < 0) {
			fprintf(stderr, "failed to get device descriptor: %s", libusb_strerror(r));
			break;
		}
		
		supported = 0;
		for (p=pcan_types;p->name != 0; p++) {
			if (p->vendor_id == ctx->dev_descr.idVendor &&
				p->product_id == ctx->dev_descr.idProduct)
			{
				supported = 1;
				break;
			}
		}
		
		if (supported) {
			if (list_devices) {
				printf("%d: %04x:%04x Bus %03d Device %03d \"%s\"\n", i, ctx->dev_descr.idVendor, ctx->dev_descr.idProduct, 
					   libusb_get_bus_number(devices[c]), libusb_get_device_address(devices[c]),
					   p->name);
			}
			
			if (i == device_idx) {
				*pcan_type = p;
				*device = devices[c];
				
				r = libusb_open(devices[c], dev_handle);
				if (r < 0) {
					printf("error opening device: %s\n", libusb_strerror(r));
				}
			}
			i++;
		}
		
		c++;
	}
	
	return 0;
}

void help(FILE *fd) {
	fprintf(fd, "Usage: pcan-id [options]\n");
	fprintf(fd, "\n");
	fprintf(fd, "Options:\n");
	fprintf(fd, "\n");
	fprintf(fd, "-h           Show this help\n");
	fprintf(fd, "-d <number>  Device index (default: 0)\n");
	fprintf(fd, "-i <number>  Set device id\n");
	fprintf(fd, "-l           List devices\n");	
	fprintf(fd, "-q           Query serial number and device id\n");
	fprintf(fd, "-s <number>  Set serial number\n");
}

char parse_long(char *arg, uint32_t *value) {
	long long int val;
	char *endptr;
	
	if ('0' <= arg[0] && arg[0] <= '9') {
		if (arg[0] == '0' && arg[1] == 'x') {
			val = strtoll(arg, &endptr, 16);
		} else {
			val = strtoll(arg, &endptr, 10);
		}
		
		if (endptr) {
			if (errno == ERANGE && (val == LLONG_MAX || val == LLONG_MIN)) {
				fprintf(stderr, "range error\n");
				return 1;
			}
			
			if (errno != 0 && val == 0) {
				fprintf(stderr, "No digits were found\n");
				return 1;
			}
			
			if (endptr == arg) {
				fprintf(stderr, "No digits were found\n");
				return 1;
			}
		}
	} else {
		fprintf(stderr, "invalid argument: %s\n", arg);
		return 1;
	}
	
	*value = val;
	
	return 0;
}

int main(int argc, char **argv) {
	int r, opt;
	uint32_t device_idx;
	uint8_t device_id;
	uint32_t serial_nr;
	unsigned char pkt[16];
	int transferred;
	struct pcan_ctx *ctx;
	uint32_t uint32;
	
	
	device_idx = 0;
	unsigned char action = 0;
	while ((opt = getopt(argc, argv, "hi:s:d:lq")) != -1) {
		switch (opt) {
			case 'h':
				help(stdout);
				return 0;
			case 'd':
				if (parse_long(optarg, &device_idx))
					exit(1);
				
				break;
			case 's':
				if (parse_long(optarg, &serial_nr))
					exit(1);
				
				action = 's';
				break;
			case 'i':
				if (parse_long(optarg, &uint32))
					exit(1);
				
				if (uint32 >= UCHAR_MAX) {
					fprintf(stderr, "invalid device id: %u < %u\n", uint32, UCHAR_MAX);
					exit(1);
				}
				device_id = uint32;
				
				action = 'i';
				break;
			case 'l':
				action = 'l';
				break;
			case 'q':
				action = 'q';
				break;
			default:
				fprintf(stderr, "unknown option: %c\n", opt);
				help(stderr);
				return 1;
		}
	}
	
	if (action == 0) {
		fprintf(stderr, "Please specify either -l, -s or -i.\n\n");
		help(stderr);
		return 1;
	}
	
	ctx = malloc(sizeof(struct pcan_ctx));
	
	r = libusb_init(&ctx->usb_ctx);
	if (r != 0) {
		fprintf(stderr, "error initializing libusb\n");
		free(ctx);
		return 1;
	}

	libusb_set_debug(ctx->usb_ctx, 3);
	
	r = browse_devices(ctx, device_idx, &ctx->device, &ctx->dev_handle, &ctx->pcan_type, (action == 'l'));
	if (r != 0)
		return r;
	
	if (!ctx->dev_handle) {
		fprintf(stderr, "error, requested device not found\n");
		return 1;
	}
	
	if (action == 'l')
		return 0;
	
	#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000104)
	libusb_set_auto_detach_kernel_driver(ctx->dev_handle, 1);
	#endif
	libusb_claim_interface(ctx->dev_handle, 0);
	libusb_reset_device(ctx->dev_handle);
	
	
	r = libusb_get_device_descriptor(ctx->device, &ctx->dev_descr);
	if (r < 0) {
		fprintf(stderr, "failed to get device descriptor: %s", libusb_strerror(r));
		return 1;
	}
	
	r = libusb_get_config_descriptor(ctx->device, 0, &ctx->config_descr);
	if (r != 0) {
		fprintf(stderr, "error, get_config_descriptor failed\n");
		return 1;
	}
	
	if (ctx->dev_descr.iManufacturer) {
		char buf[64];
		libusb_get_string_descriptor_ascii(ctx->dev_handle, ctx->dev_descr.iManufacturer, buf, sizeof(buf));
		printf("%20s: %s\n", "iManufacturer", buf);
	}
	if (ctx->dev_descr.iProduct) {
		char buf[64];
		libusb_get_string_descriptor_ascii(ctx->dev_handle, ctx->dev_descr.iProduct, buf, sizeof(buf));
		printf("%20s: %s\n", "iProduct", buf);
	}
	printf("\n");
	
	
	if (action == 'i') {
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = 4;
		pkt[1] = 2;
		pkt[2] = device_id;
		
		r = libusb_bulk_transfer(ctx->dev_handle,
							0x1,
							pkt,
							sizeof(pkt),
							&transferred,
							USB_TIMEOUT_MS);
		
		if (r != LIBUSB_SUCCESS)
			printf("error %s\n", libusb_error_name(r));
	}
	
	if (action == 's') {
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = 6;
		pkt[1] = 2;
		
		serial_nr = htole32(serial_nr);
		memcpy(&pkt[2], &serial_nr, sizeof(serial_nr));
		
		r = libusb_bulk_transfer(ctx->dev_handle,
							0x1,
							pkt,
							sizeof(pkt),
							&transferred,
							USB_TIMEOUT_MS);
		
		if (r != LIBUSB_SUCCESS)
			printf("error %s\n", libusb_error_name(r));
	}
	
	if (action == 'q') {
		/*
		 * request device id
		 */
		
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = 4;
		pkt[1] = 1;
		
		// send request
		r = libusb_bulk_transfer(ctx->dev_handle,
							0x1,
							pkt,
							sizeof(pkt),
							&transferred,
							USB_TIMEOUT_MS);
		
		if (r != LIBUSB_SUCCESS)
			printf("error %s\n", libusb_error_name(r));
		
		// get response
		r = libusb_bulk_transfer(ctx->dev_handle,
							0x81,
							pkt,
							sizeof(pkt),
							&transferred,
							USB_TIMEOUT_MS);
		
		if (r != LIBUSB_SUCCESS)
			printf("error %s\n", libusb_error_name(r));
		
		device_id = pkt[2];
		printf("%20s: 0x%x\n", "device_id", device_id);
		
		
		
		/*
		 * request serial number
		 */
		
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = 6;
		pkt[1] = 1;
		
		// send request
		r = libusb_bulk_transfer(ctx->dev_handle,
							0x1,
							pkt,
							sizeof(pkt),
							&transferred,
							USB_TIMEOUT_MS);
		
		if (r != LIBUSB_SUCCESS)
			printf("error %s\n", libusb_error_name(r));
		
		// get response
		r = libusb_bulk_transfer(ctx->dev_handle,
							0x81,
							pkt,
							sizeof(pkt),
							&transferred,
							USB_TIMEOUT_MS);
		
		if (r != LIBUSB_SUCCESS)
			printf("error %s\n", libusb_error_name(r));
		
		memcpy(&serial_nr, &pkt[2], sizeof(serial_nr));
		
		serial_nr = le32toh(serial_nr);
		printf("%20s: 0x%x\n", "serial_number", serial_nr);
	}
	
	libusb_free_config_descriptor(ctx->config_descr);
	
	libusb_attach_kernel_driver(ctx->dev_handle, 1);
	
	libusb_release_interface(ctx->dev_handle, 0);
	if (ctx->dev_handle)
		libusb_close(ctx->dev_handle);
	
	libusb_exit(ctx->usb_ctx);
}
