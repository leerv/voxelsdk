/*
 * TI Voxel Lib component.
 *
 * Copyright (c) 2014 Texas Instruments Inc.
 */

#include "TintinCDKCamera.h"
#include "VoxelXUProgrammer.h"
#include "VoxelUSBProgrammer.h"
#include <Logger.h>
#include <UVCStreamer.h>
#include <USBBulkStreamer.h>

#include <Parameter.h>
#define _USE_MATH_DEFINES
#include <math.h>
namespace Voxel
{
  
namespace TI
{
  
TintinCDKCamera::TintinCDKCamera(Voxel::DevicePtr device): ToFTintinCamera("TintinCDKCamera", device)
{
  _init();
}

class TintinCDKMixVoltageParameter: public UnsignedIntegerParameter
{
protected:
  virtual uint _fromRawValue(uint32_t value) const
  {
    if(value > 0x80U)
      return (value - 0x80U)*50 + 500;
    else
      return 500;
  }
  
  virtual uint32_t _toRawValue(uint value) const
  {
    if(value > 500)
      return (value - 500)/50 + 0x80U;
    else
      return 0x80U;
  }
  
public:
  TintinCDKMixVoltageParameter(RegisterProgrammer &programmer):
  UnsignedIntegerParameter(programmer, MIX_VOLTAGE, "mV", 0x2D05, 8, 7, 0, 1200, 2000, 1500, "Mixing voltage", 
                           "Mixing voltage?", Parameter::IO_READ_WRITE, {})
  {}
  
  virtual ~TintinCDKMixVoltageParameter() {}
};


class TintinCDKPVDDParameter: public UnsignedIntegerParameter
{
protected:
  virtual uint _fromRawValue(uint32_t value) const
  {
    if(value > 0x80U)
      return (value - 0x80U)*50 + 500;
    else
      return 500;
  }
  
  virtual uint32_t _toRawValue(uint value) const
  {
    if(value > 500)
      return (value - 500)/50 + 0x80U;
    else
      return 0x80U;
  }
  
public:
  TintinCDKPVDDParameter(RegisterProgrammer &programmer):
  UnsignedIntegerParameter(programmer, PVDD, "mV", 0x2D0E, 8, 7, 0, 2000, 3600, 3300, "Pixel VDD", 
                           "Reset voltage level of pixel.", Parameter::IO_READ_WRITE, {})
  {}
  
  virtual ~TintinCDKPVDDParameter() {}
};

class TintinCDKIlluminationPowerParameter: public UnsignedIntegerParameter
{
protected:
  // Relations derived by Subhash
  virtual uint _fromRawValue(uint32_t value) const
  {
    float R = value*100.0/255; //Digipot resistance in kOhms for given register setting
    float Rlim = R*113.0/(R+113)+51.1; //Effective current limiting resistor in kOhms for the given digipot resistance
    float I = 118079.0e-3/Rlim; //Current limit implemented by the linear current limiting IC for the given limiting resistance. This has a small variation as compared to the datasheet. That assumption has been made for the purpose of simplification.
    return (I-0.2)*1100; //Subtracting the threshold current and multiplying by the power gain of the laser diode.
  }
  
  virtual uint32_t _toRawValue(uint value) const
  {
    float v = value/1000.0;
    return uint32_t(((130000/(v + 0.22))-51.1e3)*28815e3/((164.1e3 - 130000/(v + 0.22))*100e3));
  }
  
public:
  TintinCDKIlluminationPowerParameter(RegisterProgrammer &programmer):
  UnsignedIntegerParameter(programmer, ILLUM_POWER, "mW", 0x2D10, 8, 7, 0, 1050, 2200, 1129, "Illumination Power", 
                           "These power numbers are approximate (+- 20%) and the actual power numbers are subject to component tolerances.", Parameter::IO_READ_WRITE, {})
  {}
  
  virtual ~TintinCDKIlluminationPowerParameter() {}
};


class TintinCDKIlluminationPowerPercentParameter: public UnsignedIntegerParameter
{
protected:
  TintinCDKCamera &_depthCamera;
  
public:
  TintinCDKIlluminationPowerPercentParameter(TintinCDKCamera &depthCamera, RegisterProgrammer &programmer):
  UnsignedIntegerParameter(programmer, ILLUM_POWER_PERCENTAGE, "%", 0, 0, 0, 0, 48, 100, 51, "Illumination Power", 
                           "These power numbers are approximate (+- 20%) and the actual power numbers are subject to component tolerances.", Parameter::IO_READ_WRITE, {}),
                           _depthCamera(depthCamera)
  {}
  
  virtual bool get(uint &value, bool refresh = false)
  {
    uint v;
    TintinCDKIlluminationPowerParameter *p = dynamic_cast<TintinCDKIlluminationPowerParameter *>(_depthCamera.getParam(ILLUM_POWER).get());
    if(!p || !p->get(v))
      return false;
    
    value = v*100/p->upperLimit();
    return true;
  }
  
  virtual bool set(const uint &value)
  {
    TintinCDKIlluminationPowerParameter *p = dynamic_cast<TintinCDKIlluminationPowerParameter *>(_depthCamera.getParam(ILLUM_POWER).get());
    if(!p)
      return false;
    
    uint v = value*p->upperLimit()/100;
    
    if(!p->set(v))
      return false;
    
    return true;
  }
  
  virtual ~TintinCDKIlluminationPowerPercentParameter() {}
};


bool TintinCDKCamera::_init()
{
  USBDevice &d = (USBDevice &)*_device;
  
  DevicePtr controlDevice = _device;
  
  if (d.productID() == TINTIN_CDK_PRODUCT_UVC) {
    _programmer = Ptr<RegisterProgrammer>(new VoxelXUProgrammer(
      { {0x2D, 1}, {0x52, 1}, {0x54, 1}, {0x4B, 2}, {0x4E, 2}, {0x58, 3}, {0x5C, 3} },
      controlDevice));
    _streamer = Ptr<Streamer>(new UVCStreamer(controlDevice));
  } else {
    USBIOPtr usbIO(new USBIO(controlDevice));

    _programmer = Ptr<RegisterProgrammer>(new VoxelUSBProgrammer(
      { {0x2D, 1}, {0x52, 1}, {0x54, 1}, {0x4B, 2}, {0x4E, 2}, {0x58, 3}, {0x5C, 3} },
      {
        {0x58, {0x08, 0x09, 0}},
        {0x5C, {0x08, 0x09, 0}},
        {0x4B, {0x0A, 0x0B, 0}},
        {0x4E, {0x0A, 0x0B, 0}},
        {0x2D, {0x04, 0x03, 8}},
        {0x52, {0x04, 0x03, 8}},
        {0x54, {0x04, 0x03, 8}}
      }, usbIO, controlDevice));
    _streamer = Ptr<Streamer>(new USBBulkStreamer(usbIO, controlDevice, 0x82));
  }
  
  if(!_programmer->isInitialized() || !_streamer->isInitialized())
    return false;
  
  if(!_addParameters({
    ParameterPtr(new TintinCDKMixVoltageParameter(*_programmer)),
    ParameterPtr(new TintinCDKPVDDParameter(*_programmer)),
    ParameterPtr(new TintinCDKIlluminationPowerParameter(*_programmer)),
    ParameterPtr(new TintinCDKIlluminationPowerPercentParameter(*this, *_programmer)),
    }))
  {
    return false;
  }
  
  // Settings for mix voltage and PVDD
  if(!_programmer->writeRegister(0x2D06, 0xFF) || 
    !_programmer->writeRegister(0x2D04, 0x40) ||
    !_programmer->writeRegister(0x2D05, 0x94) ||
    !_programmer->writeRegister(0x2D0D, 0x40) || 
    !_programmer->writeRegister(0x2D0E, 0xB8) ||
    !_programmer->writeRegister(0x2D0F, 0xFF)
  )
    return false;
    
  if(!ToFTintinCamera::_init())
    return false;
  
  return true;
}

bool TintinCDKCamera::_initStartParams()
{
  USBDevice &d = (USBDevice &)*_device;

  if(!ToFTintinCamera::_initStartParams())
    return false;

  if (!set(TILLUM_SLAVE_ADDR, 0x72U))
    return false;

  if ((d.productID() == TINTIN_CDK_PRODUCT_BULK) && !set(BLK_HEADER_EN, false))
    return false;

  return true;
}



bool TintinCDKCamera::_getFieldOfView(float &fovHalfAngle) const
{
  fovHalfAngle = (87/2.0f)*(M_PI/180.0f);
  return true;
}

bool TintinCDKCamera::_setStreamerFrameSize(const FrameSize &s)
{
  UVCStreamer *uvcStreamer = dynamic_cast<UVCStreamer *>(&*_streamer);
  USBBulkStreamer *bulkStreamer = dynamic_cast<USBBulkStreamer *>(&*_streamer);
  USBDevice &d = (USBDevice &)*_device;
  
  VideoMode m;
  m.frameSize = s;
  
  int bytesPerPixel;
  
  if(!_get(PIXEL_DATA_SIZE, bytesPerPixel))
  {
    logger(LOG_ERROR) << "TintinCDKCamera: Could not get current bytes per pixel" << std::endl;
    return false;
  }
  
  if(!_getFrameRate(m.frameRate))
  {
    logger(LOG_ERROR) << "TintinCDKCamera: Could not get current frame rate" << std::endl;
    return false;
  }
  
  if ((d.productID() == TINTIN_CDK_PRODUCT_UVC)) 
  {
    if(bytesPerPixel == 4)
      m.frameSize.width *= 2;
    if (!uvcStreamer) 
    {
      logger(LOG_ERROR) << "TintinCDKCamera: Streamer is not of type UVC" << std::endl;
      return false;
    }
    
    if(!uvcStreamer->setVideoMode(m)) 
    {
      logger(LOG_ERROR) << "TintinCDKCamera: Could not set video mode for UVC" << std::endl;
      return false;
    }
  } 
  else if ((d.productID() == TINTIN_CDK_PRODUCT_BULK)) 
  {
    if (!bulkStreamer) 
    {
      logger(LOG_ERROR) << "TintinCDKCamera: Streamer is not of type Bulk" << std::endl;
      return false;
    }
  
    if (!bulkStreamer->setBufferSize(s.width * s.height * bytesPerPixel)) 
    {
      logger(LOG_ERROR) << "TintinCDKCamera: Could not set buffer size for bulk transfer" << std::endl;
      return false;
    }
  }
  
  return true;
}

bool TintinCDKCamera::_getSupportedVideoModes(Vector<SupportedVideoMode> &supportedVideoModes) const
{
  supportedVideoModes = Vector<SupportedVideoMode> {
    SupportedVideoMode(320,240,25,1,4),
    SupportedVideoMode(160,240,50,1,4),
    SupportedVideoMode(160,120,100,1,4),
    SupportedVideoMode(80,120,200,1,4),
    SupportedVideoMode(80,60,400,1,4),
    SupportedVideoMode(320,240,50,1,2),
    SupportedVideoMode(320,120,100,1,2),
    SupportedVideoMode(160,120,200,1,2),
    SupportedVideoMode(160,60,400,1,2),
    SupportedVideoMode(80,60,400,1,2),
  };
  return true;
}

bool TintinCDKCamera::_getMaximumVideoMode(VideoMode &videoMode) const
{
  int bytesPerPixel;
  if(!_get(PIXEL_DATA_SIZE, bytesPerPixel))
  {
    logger(LOG_ERROR) << "TintinCDKCamera: Could not get current bytes per pixel" << std::endl;
    return false;
  }
  
  videoMode.frameSize.width = 320;
  videoMode.frameSize.height = 240;
  videoMode.frameRate.denominator = 1;
  videoMode.frameRate.numerator = (bytesPerPixel == 4)?25:50;
  return true;
}


  
}
}