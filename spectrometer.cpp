#include "spectrometer.hpp"

// from https://stackoverflow.com/questions/12591469/detect-system-endianness-in-one-line
inline bool isLittleEndian(void)
{
  static const int i = 1;
  static const char* const c = reinterpret_cast<const char* const>(&i);
  return (*c == 1);
}

static inline void printEndpointComp(const struct libusb_ss_endpoint_companion_descriptor *ep_comp)
{
  printf("      USB 3.0 Endpoint Companion:\n");
  printf("        bMaxBurst:        %d\n", ep_comp->bMaxBurst);
  printf("        bmAttributes:     0x%02x\n", ep_comp->bmAttributes);
  printf("        wBytesPerInterval: %d\n", ep_comp->wBytesPerInterval);
}

static inline void printEndpoint(const struct libusb_endpoint_descriptor *endpoint)
{
  printf("      Endpoint:\n");
  printf("        bEndpointAddress: %02xh\n", endpoint->bEndpointAddress);
  printf("        bmAttributes:     %02xh\n", endpoint->bmAttributes);
  printf("        wMaxPacketSize:   %d\n", endpoint->wMaxPacketSize);
  printf("        bInterval:        %d\n", endpoint->bInterval);
  printf("        bRefresh:         %d\n", endpoint->bRefresh);
  printf("        bSynchAddress:    %d\n", endpoint->bSynchAddress);

  for (int i = 0; i < endpoint->extra_length;) {
    if (LIBUSB_DT_SS_ENDPOINT_COMPANION == endpoint->extra[i + 1]) {
      struct libusb_ss_endpoint_companion_descriptor *ep_comp;

      int ret = libusb_get_ss_endpoint_companion_descriptor(NULL, endpoint, &ep_comp);
      if (LIBUSB_SUCCESS != ret) continue;

      printEndpointComp(ep_comp);

      libusb_free_ss_endpoint_companion_descriptor(ep_comp);
    }

    i += endpoint->extra[i];
  }
}

static inline void printAltsetting(const struct libusb_interface_descriptor *interface)
{
  printf("    Interface:\n");
  printf("      bInterfaceNumber:   %d\n", interface->bInterfaceNumber);
  printf("      bAlternateSetting:  %d\n", interface->bAlternateSetting);
  printf("      bNumEndpoints:      %d\n", interface->bNumEndpoints);
  printf("      bInterfaceClass:    %d\n", interface->bInterfaceClass);
  printf("      bInterfaceSubClass: %d\n", interface->bInterfaceSubClass);
  printf("      bInterfaceProtocol: %d\n", interface->bInterfaceProtocol);
  printf("      iInterface:         %d\n", interface->iInterface);

  for (int i = 0; i < interface->bNumEndpoints; ++i)
    printEndpoint(&interface->endpoint[i]);
}

static inline void print_2_0_ext_cap(struct libusb_usb_2_0_extension_descriptor *usb_2_0_ext_cap)
{
  printf("    USB 2.0 Extension Capabilities:\n");
  printf("      bDevCapabilityType: %d\n", usb_2_0_ext_cap->bDevCapabilityType);
  printf("      bmAttributes:       0x%x\n", usb_2_0_ext_cap->bmAttributes);
}

static inline void print_ss_usb_cap(struct libusb_ss_usb_device_capability_descriptor *ss_usb_cap)
{
  printf("    USB 3.0 Capabilities:\n");
  printf("      bDevCapabilityType: %d\n", ss_usb_cap->bDevCapabilityType);
  printf("      bmAttributes:       0x%x\n", ss_usb_cap->bmAttributes);
  printf("      wSpeedSupported:    0x%x\n", ss_usb_cap->wSpeedSupported);
  printf("      bFunctionalitySupport: %d\n", ss_usb_cap->bFunctionalitySupport);
  printf("      bU1devExitLat:      %d\n", ss_usb_cap->bU1DevExitLat);
  printf("      bU2devExitLat:      %d\n", ss_usb_cap->bU2DevExitLat);
}

static void print_bos(libusb_device_handle *handle)
{
  struct libusb_bos_descriptor *bos;

  int ret = libusb_get_bos_descriptor(handle, &bos);
  if (0 > ret) return;

  printf("  Binary Object Store (BOS):\n");
  printf("    wTotalLength:       %d\n", bos->wTotalLength);
  printf("    bNumDeviceCaps:     %d\n", bos->bNumDeviceCaps);

  if (bos->dev_capability[0]->bDevCapabilityType == LIBUSB_BT_USB_2_0_EXTENSION) {
    struct libusb_usb_2_0_extension_descriptor *usb_2_0_extension;
    ret =  libusb_get_usb_2_0_extension_descriptor(NULL, bos->dev_capability[0],&usb_2_0_extension);
    if (0 > ret) return;

    print_2_0_ext_cap(usb_2_0_extension);
    libusb_free_usb_2_0_extension_descriptor(usb_2_0_extension);
  }

  if (bos->dev_capability[0]->bDevCapabilityType == LIBUSB_BT_SS_USB_DEVICE_CAPABILITY) {
    struct libusb_ss_usb_device_capability_descriptor *dev_cap;
    ret = libusb_get_ss_usb_device_capability_descriptor(NULL, bos->dev_capability[0],&dev_cap);
    if (0 > ret) return;

    print_ss_usb_cap(dev_cap);
    libusb_free_ss_usb_device_capability_descriptor(dev_cap);
  }

  libusb_free_bos_descriptor(bos);
}

static void printInterface(const struct libusb_interface *interface)
{
  for (int i = 0; i < interface->num_altsetting; ++i)
    printAltsetting(&interface->altsetting[i]);
}

static void printConfiguration(struct libusb_config_descriptor *config)
{
  printf("  Configuration:\n");
  printf("    wTotalLength:         %d\n", config->wTotalLength);
  printf("    bNumInterfaces:       %d\n", config->bNumInterfaces);
  printf("    bConfigurationValue:  %d\n", config->bConfigurationValue);
  printf("    iConfiguration:       %d\n", config->iConfiguration);
  printf("    bmAttributes:         %02xh\n", config->bmAttributes);
  printf("    MaxPower:             %d\n", config->MaxPower);

  for (int i = 0; i < config->bNumInterfaces; ++i)
    printInterface(&config->interface[i]);
}

static int printDevice(libusb_device *dev, int level)
{
  struct libusb_device_descriptor desc;
  libusb_device_handle *handle = NULL;
  char description[260];
  unsigned char string[256];
  int ret;
  uint8_t i;

  ret = libusb_get_device_descriptor(dev, &desc);
  if (ret < 0) {
    fprintf(stderr, "failed to get device descriptor");
    return -1;
  }

  ret = libusb_open(dev, &handle);
  if (LIBUSB_SUCCESS == ret) {
    if (desc.iManufacturer) {
      ret = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, string, sizeof(string));
      if (ret > 0)
	snprintf(description, sizeof(description), "%s - ", string);
      else
	snprintf(description, sizeof(description), "%04X - ",
		 desc.idVendor);
    } else {
      snprintf(description, sizeof(description), "%04X - ",
	       desc.idVendor);
    }

    if (desc.iProduct) {
      ret = libusb_get_string_descriptor_ascii(handle, desc.iProduct, string, sizeof(string));
      if (ret > 0)
	snprintf(description + strlen(description), sizeof(description) -
		 strlen(description), "%s", string);
      else
	snprintf(description + strlen(description), sizeof(description) -
		 strlen(description), "%04X", desc.idProduct);
    } else {
      snprintf(description + strlen(description), sizeof(description) -
	       strlen(description), "%04X", desc.idProduct);
    }
  } else {
    snprintf(description, sizeof(description), "%04X - %04X",
	     desc.idVendor, desc.idProduct);
  }

  printf("%.*sDev (bus %d, device %d): %s\n", level * 2, "                    ",
	 libusb_get_bus_number(dev), libusb_get_device_address(dev), description);

  printf("Port Numbers: ");
  uint8_t port_numbers[100];
  int n = libusb_get_port_numbers(dev, port_numbers, 100);
  for (int i = 0; i < n; ++i) {
    printf("%d ", port_numbers[i]);
  }
  
  if (handle) {
    if (desc.iSerialNumber) {
      ret = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, string, sizeof(string));
      if (ret > 0)
	printf("%.*s  - Serial Number: %s\n", level * 2,
	       "                    ", string);
    }
  }

  for (i = 0; i < desc.bNumConfigurations; ++i) {
    struct libusb_config_descriptor *config;
    ret = libusb_get_config_descriptor(dev, i, &config);
    if (LIBUSB_SUCCESS != ret) {
      printf("  Couldn't retrieve descriptors\n");
      continue;
    }

    printConfiguration(config);

    libusb_free_config_descriptor(config);
  }

  if (handle && desc.bcdUSB >= 0x0201) {
    print_bos(handle);
  }

  if (handle)
    libusb_close(handle);

  return 0;
}

namespace spectrometer {
  
  static int usbCount = 0;
  static libusb_device **usbDevices = NULL;
  
  void initializeUSBStack(void)
  {
    int error_code = libusb_init(NULL);
    if (error_code < 0)
      throw std::runtime_error("libusb initialization is failed!");
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    assert(isLittleEndian());
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    assert(!isLittleEndian());
#else
# error "Undefined the endianness of compile machine!"
#endif
  }

  int findDevices(bool verbose)
  {
    if (usbDevices) {
      libusb_free_device_list(usbDevices, 1);
      usbCount = 0;
    }

    usbCount = libusb_get_device_list(NULL, &usbDevices);
    if (usbCount <= 0) {
      throw std::runtime_error("Nothing to be found on USB ports!");
      //libusb_exit(NULL);
    }

    if (verbose) {
      for (int i = 0; i < usbCount; ++i) {
	libusb_device *dev = usbDevices[i];
	printDevice(dev, 0);
	printf("\n");
      }
    }
    return usbCount;
  }

  libusb_device* findDevice(int vid, int pid, int index)
  {
    if (usbDevices) {
      libusb_free_device_list(usbDevices, 1);
      usbCount = 0;
    }

    usbCount = libusb_get_device_list(NULL, &usbDevices);
    if (usbCount <= 0) {
      throw std::runtime_error("Nothing to be found on USB ports!");
      //libusb_exit(NULL);
    }

    libusb_device *foundDevice = NULL;
    for (int i = 0; i < usbCount; ++i) {
      libusb_device *dev = usbDevices[i];
      struct libusb_device_descriptor desc;
      libusb_get_device_descriptor(dev, &desc);
      if (desc.idVendor == vid && desc.idProduct == pid) {
	if (index == 0) {
	  foundDevice = dev;
	  break;
	}
	--index;
      }
    }

    return foundDevice;
  }
  
  libusb_device* filterDevice(int vid, int pid, int index)
  {
    if (!usbDevices) return NULL;

    libusb_device *foundDevice = NULL;
    for (int i = 0; i < usbCount; ++i) {
      libusb_device *dev = usbDevices[i];
      struct libusb_device_descriptor desc;
      libusb_get_device_descriptor(dev, &desc);
      if (desc.idVendor == vid && desc.idProduct == pid) {
	if (index == 0) {
	  foundDevice = dev;
	  break;
	}
	--index;
      }
    }

    if (!foundDevice)
      throw std::runtime_error("Nothing to be found such a spectrometer!");
    
    return foundDevice;
  }
  
  void deinitializeUSBStack(void)
  {
    if (usbDevices) libusb_free_device_list(usbDevices, 1);
    libusb_exit(NULL);
  }
  
}
