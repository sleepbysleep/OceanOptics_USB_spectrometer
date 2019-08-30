#include <iostream>
#include <algorithm>
#include <vector>

#include "spectrometer.hpp"

int main(void)
{
  spectrometer::initializeUSBStack();
  spectrometer::findDevices(true);
  //libusb_device *dev = spectrometer::filterDevice(spectrometer::usb4kVID, spectrometer::usb4kPID, 0);
  //spectrometer::usb4k spec(dev);

  spectrometer::usb4k *spec = nullptr;
  
  spec = new spectrometer::usb4k;
  spec->setIntegration(3800);
  spec->setTriggerMode(spectrometer::usb4k::NORMAL_TRIGGER);
  
  try {
    std::vector<std::array<uint16_t, spectrometer::usb4kPixelCount>> stack_spec(100);
    std::array<float, spectrometer::usb4kPixelCount> dark_correct;
    std::array<float, spectrometer::usb4kPixelCount> accumulator;

    std::fill(std::begin(accumulator), std::end(accumulator), 0);
    //accumulator.fill(0);
    
    for (int i = 0; i < 100; ++i) {
      //auto raw_data = spec->getRawSpectrum();
      std::array<uint16_t, spectrometer::usb4kPixelCount> &raw_data = spec->getRawSpectrum();
    
      // Optical Black Correction by electric dark pixels
      uint32_t sum = 0;
      for (int j : spectrometer::usb4kEdarkIndices) sum += raw_data[j];
      float edarkness = (float)sum / spectrometer::usb4kEdarkIndices.size();
      std::cout << "electric darkness: " << edarkness;
      
      std::transform(std::begin(raw_data), std::end(raw_data), std::begin(dark_correct),
		     [edarkness](uint16_t v) -> float { return v - edarkness; });

      // To see a value in peak of raw_data
      float max_value = *std::max_element(std::begin(dark_correct) + spectrometer::usb4kActivePixelBegin,
					  std::begin(dark_correct) + spectrometer::usb4kActivePixelEnd);
      std::cout << ", peak value: " << max_value;

      // Accumulating raw_data
      std::transform(std::begin(accumulator), std::end(accumulator),
		     std::begin(dark_correct), std::begin(accumulator),
		     std::plus<float>()); //[](float a, float b) -> float { return a + b; });

      // To see a value in peak of accumulator
      max_value = *std::max_element(std::begin(accumulator) + spectrometer::usb4kActivePixelBegin,
				    std::begin(accumulator) + spectrometer::usb4kActivePixelEnd);
      std::cout << ", peak value in total: " << max_value;

      /* Immutability check
      stack_spec[i] = raw_data;
      stack_spec[i][0] = 0;
      std::cout << stack_spec[i][0] << "vs. " <<  raw_data[0] << std::endl;
      */
      if (spectrometer::findDevice(spectrometer::usb4kVID, spectrometer::usb4kPID, 0)) {
	std::cout << ", connection on" << std::endl;
      } else {
	std::cout << ", connection off" << std::endl;
      }
      
    }
  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
  }

  delete spec;
  spectrometer::deinitializeUSBStack();
  
  return 0;
}
