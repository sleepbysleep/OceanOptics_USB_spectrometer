#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <limits>
#include <cstdlib>
#include <cassert>
#include <stdexcept>
#include <exception>
#include <chrono>
#include <thread>

#include <libusb-1.0/libusb.h>

static int verbose = 1;

static void printEndpointComp(const struct libusb_ss_endpoint_companion_descriptor *ep_comp)
{
  printf("      USB 3.0 Endpoint Companion:\n");
  printf("        bMaxBurst:        %d\n", ep_comp->bMaxBurst);
  printf("        bmAttributes:     0x%02x\n", ep_comp->bmAttributes);
  printf("        wBytesPerInterval: %d\n", ep_comp->wBytesPerInterval);
}

static void printEndpoint(const struct libusb_endpoint_descriptor *endpoint)
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

static void printAltsetting(const struct libusb_interface_descriptor *interface)
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

static void print_2_0_ext_cap(struct libusb_usb_2_0_extension_descriptor *usb_2_0_ext_cap)
{
  printf("    USB 2.0 Extension Capabilities:\n");
  printf("      bDevCapabilityType: %d\n", usb_2_0_ext_cap->bDevCapabilityType);
  printf("      bmAttributes:       0x%x\n", usb_2_0_ext_cap->bmAttributes);
}

static void print_ss_usb_cap(struct libusb_ss_usb_device_capability_descriptor *ss_usb_cap)
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

  if (handle && verbose) {
    if (desc.iSerialNumber) {
      ret = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, string, sizeof(string));
      if (ret > 0)
	printf("%.*s  - Serial Number: %s\n", level * 2,
	       "                    ", string);
    }
  }

  if (verbose) {
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
  }

  if (handle)
    libusb_close(handle);

  return 0;
}

int deviceCount = 0;
libusb_device **usbDevices = NULL;

const int vendorID = 0x2457;
const int productID = 0x1022;
libusb_device_handle *deviceHandle = NULL;
bool needReattach = false;
int configuration = 0;  
int interface = 0;
int altsetting = 0;

static unsigned char *temperalBuffer = NULL;

static std::string serialNumber;
static float wavelengthCoeffs[4] = {0.0,};
static float lightConstant = 0.0;
static float linearityCoeffs[8] = {0.0,};
static int gratingNumber;
static int filterWavelength;
static int slitSize;

static const std::array<int, 13>  edardIndices = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
static const int pixelCount = 256*16;
static std::array<float, pixelCount> spectrumWavelengths = {0.0,};
static std::array<unsigned short, pixelCount> spectrumAmplitudes = {0,};
static int integrationTime = 0;

static inline int writeEP1(unsigned char *buf, int len, int timeout=1000)
{
  int ret, inouts;

  ret = libusb_bulk_transfer(deviceHandle, 0x01, buf, len, &inouts, timeout);
  if (ret != 0)
    throw std::runtime_error("Failed to transfer the data to out_EP1!");
  //printf("%d transferred.\n", inouts);
  return inouts;
}

static inline int readEP1(unsigned char *buf, int len, int timeout=1000)
{
  int ret, inouts;
  
  ret = libusb_bulk_transfer(deviceHandle, 0x81, buf, len, &inouts, timeout);
  if (ret != 0)
    throw std::runtime_error("Failed to recevice the data from in_EP1!");
  //printf("%d received.\n", inouts);
  return inouts;
}

static inline int readEP6(unsigned char *buf, int len, int timeout=1000)
{
  int ret, inouts;
  
  ret = libusb_bulk_transfer(deviceHandle, 0x86, buf, len, &inouts, timeout);
  if (ret != 0)
    throw std::runtime_error("Failed to recevice the data from in_EP6!");
  //printf("%d received.\n", inouts);
  return inouts;
}

static inline int readEP2(unsigned char *buf, int len, int timeout=1000)
{
  int ret, inouts;
  
  ret = libusb_bulk_transfer(deviceHandle, 0x82, buf, len, &inouts, timeout);
  if (ret != 0)
    throw std::runtime_error("Failed to recevice the data from in_EP2!");
  //printf("%d received.\n", inouts);
  return inouts;
}

static void initializeUSB4000(void)
{
  temperalBuffer[0] = 0x01;
  writeEP1(temperalBuffer, 1);
}

static int getIntegration(void)
{
  temperalBuffer[0] = 0xfe;
  writeEP1(temperalBuffer, 1);
  int len = readEP1(temperalBuffer, 64);
  assert(len >= 6);
  int usec = (temperalBuffer[5] << 24) + (temperalBuffer[4] << 16) + (temperalBuffer[3] << 8) + temperalBuffer[2];
  return usec;
}

static bool setIntegration(int usec, bool verify=false)
{
  if (usec < 10 || usec > 65535000)
    throw std::out_of_range("Integration time Out of range [10, 65535000] us!");

  temperalBuffer[0] = 0x02;
  temperalBuffer[1] = usec & 0xff;
  temperalBuffer[2] = (usec >> 8) & 0xff;
  temperalBuffer[3] = (usec >> 16) & 0xff;
  temperalBuffer[4] = (usec >> 24) & 0xff;

  writeEP1(temperalBuffer, 5);
  if (verify) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    int written = getIntegration();
    std::cout << "setIntegration: " << usec << "[us] --- written: " << written << "[us]\n";
    if (usec != written) {
      //throw std::invalid_argument("Failed to set integration");
      return false;
    }
  }

  integrationTime = usec;
  
  return true;
}

void setStrobeEnableStatus(bool enable)
{
  temperalBuffer[0] = 0x03;
  temperalBuffer[1] = enable ? 1 : 0;
  temperalBuffer[2] = 0x00;
  writeEP1(temperalBuffer, 3);
}

static float queryNumeric(unsigned char cmd)
{
  temperalBuffer[0] = 0x05, temperalBuffer[1] = cmd;
  writeEP1(temperalBuffer, 2);

  int len = readEP1(temperalBuffer, 64);
  assert(len > 2);

  /*
  for (int i = 0; i < len; ++i)
    printf("%c[%02x]%c", temperalBuffer[i], temperalBuffer[i], i < len-1 ? ' ':'\n');
  */
  
  return std::atof(reinterpret_cast<const char *>(temperalBuffer+2));
}

static std::string queryString(unsigned char cmd)
{
  temperalBuffer[0] = 0x05, temperalBuffer[1] = cmd;
  writeEP1(temperalBuffer, 2);

  int len = readEP1(temperalBuffer, 64);
  assert(len > 2);
  
  /*
  for (int i = 0; i < len; ++i)
    printf("%c[%02x]%c", temperalBuffer[i], temperalBuffer[i], i < len-1 ? ' ':'\n');
  */
  
  std::string parsed(reinterpret_cast<const char *>(temperalBuffer+2));
  
  return parsed;
}

static std::array<float, pixelCount> getWavelengths(void)
{
  return spectrumWavelengths;
}

// from https://stackoverflow.com/questions/12591469/detect-system-endianness-in-one-line
inline bool isLittleEndian(void)
{
  static const int i = 1;
  static const char* const c = reinterpret_cast<const char* const>(&i);
  return (*c == 1);
}

static std::array<unsigned short, pixelCount>& getRawSpectrum(bool request=true)
{
  if (request) {
    // request spectrum
    temperalBuffer[0] = 0x09;
    writeEP1(temperalBuffer, 1);
  }

  int i = 0, len;
  int waiting = int(integrationTime / 1000.0 * 2.1);
  static uint16_t *packet = reinterpret_cast<uint16_t *>(temperalBuffer);
  
  if (isLittleEndian()) {
    len = readEP6(temperalBuffer, 512, waiting);
    std::copy(packet, packet+256, spectrumAmplitudes.begin());
    
    for (i = 1; i < 4; ++i) {
      len = readEP6(temperalBuffer, 512);
      assert(len == 512);
      std::copy(packet, packet+256, spectrumAmplitudes.begin()+i*256);
    }
    
    for (; i < 15; ++i) {
      len = readEP2(temperalBuffer, 512);
      assert(len == 512);
      std::copy(packet, packet+256, spectrumAmplitudes.begin()+i*256);
    }
  } else {
    len = readEP6(temperalBuffer, 512, waiting);
    for (int j = 0; j < 256; ++j)
      spectrumAmplitudes[/*i*256 +*/ j] = __builtin_bswap16(packet[j]);
    
    for (i = 1; i < 4; ++i) {
      len = readEP6(temperalBuffer, 512);
      assert(len == 512);
      for (int j = 0; j < 256; ++j)
	spectrumAmplitudes[i*256 + j] = __builtin_bswap16(packet[j]);
    }
    
    for (; i < 15; ++i) {
      len = readEP2(temperalBuffer, 512);
      assert(len == 512);
      for (int j = 0; j < 256; ++j)
	spectrumAmplitudes[i*256 + j] = __builtin_bswap16(packet[j]);
    }
  }

  len = readEP2(temperalBuffer, 1);
  assert(temperalBuffer[0] == 0x69);
  
  return spectrumAmplitudes;
}

static void setTriggerMode(int mode)
{
  /*
    0: Normal Mode
    1: Software Trigger Mode
    2: External Synchronization Trigger Mode
    3: External Hardware Trigger Mode
  */
  temperalBuffer[0] = 0x02;
  temperalBuffer[1] = mode & 0xff;
  temperalBuffer[2] = (mode >> 8) & 0xff;
  writeEP1(temperalBuffer, 3);
}

static float readPCBTemperature(void)
{
  temperalBuffer[0] = 0x6c;
  writeEP1(temperalBuffer, 1);
  int len = readEP1(temperalBuffer, 3);
  assert(len == 3);
  return 0.003906 * ((temperalBuffer[2] << 8) + temperalBuffer[1]);
}

static int readFirmwareVer(void)
{
  temperalBuffer[0] = 0x6b;
  temperalBuffer[1] = 0x04;
  writeEP1(temperalBuffer, 2);
  int len = readEP1(temperalBuffer, 3);
  assert(len == 3);
  return int((temperalBuffer[2] << 8) + temperalBuffer[1]);
}

int main(void)
{
  int error_code = libusb_init(NULL);
  if (error_code < 0)
    throw std::runtime_error("libusb initialization is failed!");

#if 0
  deviceCount = libusb_get_device_list(NULL, &usbDevices);
  if (deviceCount <= 0) {
    libusb_exit(NULL);
    throw std::runtime_error("Nothing to be displayed on USB ports!");
  }

  // dump the usb devices
  for (int i = 0; i < deviceCount; ++i) {
    libusb_device *dev = usbDevices[i];
    printDevice(dev, 0);
    /*
    struct libusb_device_descriptor desc;
    int ret = libusb_get_device_descriptor(dev, &desc);
    if (ret < 0)
      throw std::runtime_error("Failed to get device descriptor!");

    std::cout << std::hex << std::setw(4) << std::setfill('0')
	      << desc.idVendor << ":"
	      << std::hex << std::setw(4) << std::setfill('0')
	      << desc.idProduct << std::endl;
    printf("%04x:%04x (bus %d, device %d)",
	   desc.idVendor, desc.idProduct,
	   libusb_get_bus_number(dev), libusb_get_device_address(dev));


    uint8_t path[8];
    ret = libusb_get_port_numbers(dev, path, sizeof(path));
    if (ret > 0) {
      printf(" path: %d", path[0]);
      for (int j = 0; j < ret; ++j)
	printf(".%d", path[j]);
    }
    */
    printf("\n");
  }
#endif
  
  int ret;
  
#if 0
  // Find the device handle with vendorID and productID
  libusb_device *foundDevice = NULL;
  for (int i = 0; i < deviceCount; ++i) {
    libusb_device *dev = usbDevices[i];
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);
    if (desc.idVendor == vendorID && desc.idProduct == productID) {
      foundDevice = dev;
      break;
    }
  }

  if (!foundDevice)
    throw std::runtime_error("Nothing to be found on USB port!");

  ret = libusb_open(foundDevice, &deviceHandle);
  if (ret != 0)
    throw std::runtime_error("Something wrong to retrieve the handle!");

#else
  // Replaceable statement for the above
  deviceHandle = libusb_open_device_with_vid_pid(NULL, vendorID, productID);
  if (!deviceHandle)
    throw std::runtime_error("Failed to open the device!");
#endif

  ret = libusb_reset_device(deviceHandle);
  if (ret != 0)
    throw std::runtime_error("Something wrong to reset the device!");
  
  libusb_get_configuration(deviceHandle, &configuration);
  ret = libusb_set_configuration(deviceHandle, configuration);
  if (ret != 0) 
    throw std::runtime_error("Failed to set the configuration!");

  // Replace the kernel driver if there is
  ret = libusb_kernel_driver_active(deviceHandle, 0);
  if (ret) {
    ret = libusb_detach_kernel_driver(deviceHandle, 0);
    if (ret == 0) needReattach = true;
    else throw std::runtime_error("Failed to detach kernel driver!");
  }
  
  ret = libusb_claim_interface(deviceHandle, interface);
  if (ret != 0)
    throw std::runtime_error("Failed to set the interface!");

  ret = libusb_set_interface_alt_setting(deviceHandle, interface, altsetting);
  if (ret != 0)
    throw std::runtime_error("Failed to set the interfance and the alt-setting!");
  
  /*
  std::cout << "Active configuration: " << configuration << std::endl;
  std::cout << "Active interface: " << interface << std::endl;
  std::cout << "Active alt-setting: " << altsetting << std::endl;
  */
  
  // TODO:
  temperalBuffer = new unsigned char[512];
  
  //libusb_set_debug(NULL, 0);
  initializeUSB4000();

  integrationTime = getIntegration();
  //setIntegration(1000, true);
  
  serialNumber = queryString(0x00);
  std::cout << "serial number: " << serialNumber << std::endl;
  
  wavelengthCoeffs[0] = queryNumeric(0x01);
  wavelengthCoeffs[1] = queryNumeric(0x02);
  wavelengthCoeffs[2] = queryNumeric(0x03);
  wavelengthCoeffs[3] = queryNumeric(0x04);

  for (int i = 0; i < pixelCount; ++i) {
    spectrumWavelengths[i] = wavelengthCoeffs[0];
    spectrumWavelengths[i] += i*wavelengthCoeffs[1];
    spectrumWavelengths[i] += i*i*wavelengthCoeffs[2];
    spectrumWavelengths[i] += i*i*i*wavelengthCoeffs[3];
    //std::cout << spectrumWavelengths[i] << (i < pixelCount-1 ? ',':'\n');
  }
  
  lightConstant = queryNumeric(0x05);
  
  linearityCoeffs[0] = queryNumeric(0x06);
  linearityCoeffs[1] = queryNumeric(0x07);
  linearityCoeffs[2] = queryNumeric(0x08);
  linearityCoeffs[3] = queryNumeric(0x09);
  linearityCoeffs[4] = queryNumeric(0x0a);
  linearityCoeffs[5] = queryNumeric(0x0b);
  linearityCoeffs[6] = queryNumeric(0x0c);
  linearityCoeffs[7] = queryNumeric(0x0d);
  
  /*
  std::cout << "0th order Wavelength Calibration Coefficient: " << wavelengthCoeffs[0] << std::endl;
  std::cout << "1st order Wavelength Calibration Coefficient: " << wavelengthCoeffs[1] << std::endl;
  std::cout << "2nd order Wavelength Calibration Coefficient: " << wavelengthCoeffs[2] << std::endl;
  std::cout << "3rd order Wavelength Calibration Coefficient: " << wavelengthCoeffs[3] << std::endl;
  std::cout << "Stray light constant: " << lightConstant << std::endl;
  std::cout << "0th order non-linearity correction coefficient: " << linearityCoeffs[0] << std::endl;
  std::cout << "1st order non-linearity correction coefficient: " << linearityCoeffs[1] << std::endl;
  std::cout << "2nd order non-linearity correction coefficient: " << linearityCoeffs[2] << std::endl;
  std::cout << "3rd order non-linearity correction coefficient: " << linearityCoeffs[3] << std::endl;
  std::cout << "4th order non-linearity correction coefficient: " << linearityCoeffs[4] << std::endl;
  std::cout << "5th order non-linearity correction coefficient: " << linearityCoeffs[5] << std::endl;
  std::cout << "6th order non-linearity correction coefficient: " << linearityCoeffs[6] << std::endl;
  std::cout << "7th order non-linearity correction coefficient: " << linearityCoeffs[7] << std::endl;
  */
  
  std::istringstream optical_config(queryString(0x0f));
  
  std::string s;
  std::getline(optical_config, s, ' ');
  gratingNumber = std::stoi(s);
  std::getline(optical_config, s, ' ');
  filterWavelength = std::stoi(s);
  std::getline(optical_config, s, ' ');
  slitSize = std::stoi(s);
  std::cout << "Optical bench configuration: " << optical_config.str() << std::endl;
  std::cout << " grating #: " << gratingNumber << ", filter wavelength: " << filterWavelength << ", slit size: " << slitSize << std::endl;

  std::string usb4000_config = queryString(0x10);
  std::cout << "USB4000 configuration: " << usb4000_config << std::endl;
  std::cout << "Firmware Ver.: " << readFirmwareVer() << std::endl;

  auto start = std::chrono::high_resolution_clock::now();
  float pcb_temp = readPCBTemperature();
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> elapsed = end-start;
  std::cout << "PCB Temperature: " << pcb_temp;
  std::cout << ", elapsed[us]: " << elapsed.count() << "\n";

  start = std::chrono::high_resolution_clock::now();
  std::array<unsigned short, pixelCount> raw_spec = getRawSpectrum();
  end = std::chrono::high_resolution_clock::now();
  elapsed = end-start;
  std::cout << "Spectrum is red, elapsed[us]: " << elapsed.count();
  std::cout << ", with integration time[us]: " << integrationTime << std::endl;

  /*
  std::fstream spec_file("spec.txt", spec_file.out | spec_file.trunc);
  for (unsigned short i: raw_spec)
    spec_file << i << "\n";
  spec_file.close();
  */
  
  // deinitializing
  delete [] temperalBuffer;
  if (deviceHandle) libusb_release_interface(deviceHandle, interface);
  if (needReattach) libusb_attach_kernel_driver(deviceHandle, 0);
  if (deviceHandle) libusb_close(deviceHandle);
  if (usbDevices) libusb_free_device_list(usbDevices, 1);
  
  libusb_exit(NULL);
  
  return 0;
}
