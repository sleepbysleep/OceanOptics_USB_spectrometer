#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <array>

#include <chrono>
#include <thread>

#include <cstring>
#include <cassert>

#include <libusb-1.0/libusb.h>

namespace spectrometer {
  void initializeUSBStack(void);

  constexpr int usb4kVID = 0x2457;
  constexpr int usb4kPID = 0x1022;
  
  int findDevices(bool verbose=false);
  libusb_device* findDevice(int vid, int pid, int index);
  libusb_device* filterDevice(int vid, int pid, int index);
  void deinitializeUSBStack(void);

  constexpr int usb4kPixelCount = 256*15;
  constexpr std::array<int, 13> usb4kEdarkIndices = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
  constexpr int usb4kActivePixelBegin = 21;
  constexpr int usb4kActivePixelEnd = 3669;
  constexpr int usb4kDefaultTimeout = 10;
  
  class usb4k {
  private:
    libusb_device_handle *deviceHandle = NULL;
    bool needReattach = false;

    int busNumber = -1;
    uint8_t portNumbers[10];
    int portCount = -1;
    
    int configuration = 0;
    int interface = 0;
    int altsetting = 0;

    uint8_t *temperalBuffer = NULL;
    std::string serialNumber;
    float wavelengthCoeffs[4];
    float lightConstant;
    float linearityCoeffs[8];
    
    int gratingNumber;
    int filterWavelength;
    int slitSize;

    //int firmwareVersion;

    std::array<float, usb4kPixelCount> spectrumWavelengths;
    std::array<uint16_t, usb4kPixelCount> spectrumAmplitudes;
    int integrationTime;
    
    inline int writeEP1(uint8_t *buf, int len, int timeout=usb4kDefaultTimeout) {
      int ret, inouts;
      ret = libusb_bulk_transfer(deviceHandle, 0x01, buf, len, &inouts, timeout);
      if (ret != 0)
	throw std::runtime_error("Failed to transfer the data to out_EP1!");
      //printf("%d transferred.\n", inouts);
      return inouts;
    }
    
    inline int readEP1(uint8_t *buf, int len, int timeout=usb4kDefaultTimeout) {
      int ret, inouts;
      ret = libusb_bulk_transfer(deviceHandle, 0x81, buf, len, &inouts, timeout);
      if (ret != 0)
    throw std::runtime_error("Failed to receive the data from in_EP1!");
      //printf("%d received.\n", inouts);
      return inouts;
    }
    
    inline int readEP6(uint8_t *buf, int len, int timeout=usb4kDefaultTimeout) {
      int ret, inouts;
      ret = libusb_bulk_transfer(deviceHandle, 0x86, buf, len, &inouts, timeout);
      if (ret != 0) throw std::runtime_error("Failed to recevice the data from inEP2!");
      return inouts;
    }
    
    inline int readEP2(uint8_t *buf, int len, int timeout=1000) {
      int ret, inouts;
      ret = libusb_bulk_transfer(deviceHandle, 0x82, buf, len, &inouts, timeout);
      if (ret != 0) throw std::runtime_error("Failed to recevice the data from inEP2!");
      return inouts;
    }
    
    void initializeUSB4K(void) {
      temperalBuffer[0] = 0x01;
      writeEP1(temperalBuffer, 1);
    }
    
    std::string queryString(uint8_t cmd) {
      temperalBuffer[0] = 0x05; temperalBuffer[1] = cmd;
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
      
    float queryNumeric(uint8_t cmd) {
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
    
    libusb_device_handle* getHandle(void) {
      libusb_device_handle *handle = libusb_open_device_with_vid_pid(NULL, usb4kVID, usb4kPID);
      if (!handle) throw std::runtime_error("Failed to open the spectrometer?");
      return handle;
    }
    
    libusb_device_handle* getHandle(libusb_device *dev) {
      libusb_device_handle *handle;
      int ret = libusb_open(dev, &handle);
      if (ret != 0) throw std::runtime_error("Failed to retrieve the handle!");
      return handle;
    }
    
    void configDevice(libusb_device_handle *handle) {
      int ret = libusb_reset_device(handle);
      if (ret != 0)
	throw std::runtime_error("Something wrong to reset the spectrometer!");

      libusb_get_configuration(handle, &configuration);
      ret = libusb_set_configuration(handle, configuration);
      if (ret != 0) 
	throw std::runtime_error("Failed to set the configuration!");

      // Replace the kernel driver if there is
      ret = libusb_kernel_driver_active(handle, 0);
      if (ret) {
	ret = libusb_detach_kernel_driver(handle, 0);
	if (ret == 0) needReattach = true;
	else throw std::runtime_error("Failed to detach kernel driver!");
      }
  
      ret = libusb_claim_interface(handle, interface);
      if (ret != 0)
	throw std::runtime_error("Failed to set the interface!");

      ret = libusb_set_interface_alt_setting(handle, interface, altsetting);
      if (ret != 0)
	throw std::runtime_error("Failed to set the interfance and the alt-setting!");

      libusb_device *dev = libusb_get_device(handle);

      busNumber = libusb_get_bus_number(dev);
      portCount = libusb_get_port_numbers(dev, portNumbers, 10);
      // /sys/bus/usb/devices/x-x.x:x.x at sysfs
      
    }

    void setupDevice(void) {
      temperalBuffer = new uint8_t[512];
  
      //libusb_set_debug(NULL, 0);
      initializeUSB4K();

      this->reset();
      serialNumber = queryString(0x00);
      std::cout << "serial number: " << serialNumber << std::endl;
  
      wavelengthCoeffs[0] = queryNumeric(0x01);
      wavelengthCoeffs[1] = queryNumeric(0x02);
      wavelengthCoeffs[2] = queryNumeric(0x03);
      wavelengthCoeffs[3] = queryNumeric(0x04);

      for (int i = 0; i < usb4kPixelCount; ++i) {
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

      integrationTime = getIntegration();
      //setIntegration(1000, true);
    }
    
  public:
    usb4k(void) {
      deviceHandle = getHandle();
      configDevice(deviceHandle);
      setupDevice();
    }
    
    usb4k(libusb_device *dev) {
      deviceHandle = getHandle(dev);
      configDevice(deviceHandle);
      setupDevice();
    }
    
    virtual ~usb4k(void) {
      delete [] temperalBuffer;
      if (deviceHandle) libusb_release_interface(deviceHandle, interface);
      if (needReattach) libusb_attach_kernel_driver(deviceHandle, 0);
      if (deviceHandle) libusb_close(deviceHandle);
    }

    std::string getSysfsPath(void) {
      std::string sysfs_path("/sys/bus/usb/devices/");
      
      sysfs_path += std::to_string(busNumber) + "-";
      sysfs_path += std::to_string(portNumbers[0]);
      for (int i = 1; i < portCount; ++i)
    sysfs_path += "."+std::to_string(portNumbers[i]);

      sysfs_path += ":"+std::to_string(configuration)+"."+std::to_string(interface);
      
      return sysfs_path;
    }

    void reset(void) { temperalBuffer[0] = 0x01; writeEP1(temperalBuffer, 1); }

    int getIntegration(void) {
      temperalBuffer[0] = 0xfe;
      writeEP1(temperalBuffer, 1);
      int len = readEP1(temperalBuffer, 64);
      assert(len >= 6);
      int usec = (temperalBuffer[5] << 24) + (temperalBuffer[4] << 16) + (temperalBuffer[3] << 8) + temperalBuffer[2];
      return usec;
    }
    
    bool setIntegration(int usec, bool verify=false) {
      if (usec < 10 || usec > 65535000)
	throw std::out_of_range("Integration time Out of range [10, 65535000] us!");

      if (usec < 655000) usec = ((usec + 5) / 10) * 10;
      else usec = ((usec + 500) / 1000) * 1000;
      
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

    int readFirmwareVer(void) {
      temperalBuffer[0] = 0x6b;
      temperalBuffer[1] = 0x04;
      writeEP1(temperalBuffer, 2);
      int len = readEP1(temperalBuffer, 3);
      assert(len == 3);
      return int((temperalBuffer[2] << 8) + temperalBuffer[1]);
    }
    
    void setStrobeEnableStatus(bool enable) {
      temperalBuffer[0] = 0x03;
      temperalBuffer[1] = enable ? 1 : 0;
      temperalBuffer[2] = 0x00;
      writeEP1(temperalBuffer, 3);
    }

    enum trigger_mode {
	  NORMAL_TRIGGER = 0,
	  SW_TRIGGER = 1,
	  EXT_SYNC_TRIGGER = 2,
	  EXT_HW_TRIGGER = 3
    }; 

    void setTriggerMode(int mode) {
      /*
	0: Normal Mode
	1: Software Trigger Mode
	2: External Synchronization Trigger Mode
	3: External Hardware Trigger Mode
      */
      temperalBuffer[0] = 0x0a;
      temperalBuffer[1] = mode & 0xff;
      temperalBuffer[2] = (mode >> 8) & 0xff;
      writeEP1(temperalBuffer, 3);
    }

    float readPCBTemperature(void) {
      temperalBuffer[0] = 0x6c;
      writeEP1(temperalBuffer, 1);
      int len = readEP1(temperalBuffer, 3);
      assert(len == 3);
      return 0.003906 * ((temperalBuffer[2] << 8) + temperalBuffer[1]);
    }

    std::array<float, usb4kPixelCount>& getWavelengths(void) {
      return spectrumWavelengths;
    }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::array<uint16_t, usb4kPixelCount>& getRawSpectrum(bool request=true) {
      if (request) {
	// request spectrum
	temperalBuffer[0] = 0x09;
	writeEP1(temperalBuffer, 1);
      }

      int i = 0, len;
      int waiting = std::max(usb4kDefaultTimeout, int(integrationTime * 2.1 / 1000.0));
      //int waiting = 0;
      uint16_t *packet = reinterpret_cast<uint16_t *>(temperalBuffer);
  
      len = readEP6(temperalBuffer, 512, waiting);
      std::copy(packet, packet+256, spectrumAmplitudes.begin());
    
      for (i = 1; i < 4; ++i) {
	//std::cout << "packet: " << i << std::endl;
	len = readEP6(temperalBuffer, 512);
	assert(len == 512);
	std::copy(packet, packet+256, spectrumAmplitudes.begin()+i*256);
      }
    
      for (; i < 15; ++i) {
	//std::cout << "packet: " << i << std::endl;
	len = readEP2(temperalBuffer, 512);
	assert(len == 512);
	std::copy(packet, packet+256, spectrumAmplitudes.begin()+i*256);
      }

      len = readEP2(temperalBuffer, 1);
      assert(temperalBuffer[0] == 0x69);
  
      return spectrumAmplitudes;
    }
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    std::array<uint16_t, usb4kPixelCount>& getRawSpectrum(bool request=true) {
      if (request) {
	// request spectrum
	temperalBuffer[0] = 0x09;
	writeEP1(temperalBuffer, 1);
      }

      int i = 0, len;
      int waiting = int(integrationTime / 1000.0 * 2.1);
      uint16_t *packet = reinterpret_cast<uint16_t *>(temperalBuffer);
  
      len = readEP6(temperalBuffer, 512, waiting);
      for (int j = 0; j < 256; ++j)
	spectrumAmplitudes[/*i*256 +*/ j] = __builtin_bswap16(packet[j]);
    
      for (i = 1; i < 4; ++i) {
	std::cout << "packet: " << i << std::endl;
	len = readEP6(temperalBuffer, 512);
	assert(len == 512);
	for (int j = 0; j < 256; ++j)
	  spectrumAmplitudes[i*256 + j] = __builtin_bswap16(packet[j]);
      }
    
      for (; i < 15; ++i) {
	std::cout << "packet: " << i << std::endl;
	len = readEP2(temperalBuffer, 512);
	assert(len == 512);
	for (int j = 0; j < 256; ++j)
	  spectrumAmplitudes[i*256 + j] = __builtin_bswap16(packet[j]);
      }

      len = readEP2(temperalBuffer, 1);
      assert(temperalBuffer[0] == 0x69);
  
      return spectrumAmplitudes;
    }
#endif

    void test(int integration_us) {
      ////////////////////////////////////////////////////////////////////////////
      auto start = std::chrono::high_resolution_clock::now();
      float pcb_temp = readPCBTemperature();
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::micro> elapsed = end-start;
      
      std::cout << "PCB Temperature: " << pcb_temp;
      std::cout << ", elapsed[us]: " << elapsed.count() << "\n";

      start = std::chrono::high_resolution_clock::now();
      setIntegration(integration_us);
      end = std::chrono::high_resolution_clock::now();
      elapsed = end-start;
      std::cout << "Integration time(" << integration_us << "us) is set, elaspsed[us]: " << elapsed.count() << std::endl;
      start = std::chrono::high_resolution_clock::now();
      getRawSpectrum();
      end = std::chrono::high_resolution_clock::now();
      elapsed = end-start;
      
      std::cout << "Spectrum is red with integration time[us]: " << integrationTime;
      std::cout << ", elapsed[us]: " << elapsed.count() << std::endl;
    }
  };
}
