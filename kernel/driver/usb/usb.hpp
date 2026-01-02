#pragma once
#include <stdint.h>

namespace USB
{
struct DeviceDescriptor
{
    uint8_t length;
    uint8_t descriptor_type;
    uint16_t bcd_usb;
    uint8_t device_class;
    uint8_t device_sub_class;
    uint8_t device_protocol;
    uint8_t max_packet_size_0;
    uint16_t id_vendor;
    uint16_t id_product;
    uint16_t bcd_device;
    uint8_t manufacturer;
    uint8_t product;
    uint8_t serial_number;
    uint8_t num_configurations;
} __attribute__((packed));

struct ConfigurationDescriptor
{
    uint8_t length;
    uint8_t descriptor_type; // = 2
    uint16_t total_length;   // 関連するインターフェース等の全サイズ
    uint8_t num_interfaces;
    uint8_t configuration_value;
    uint8_t configuration_string;
    uint8_t attributes;
    uint8_t max_power;
} __attribute__((packed));

struct InterfaceDescriptor
{
    uint8_t length;
    uint8_t descriptor_type; // = 4
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t num_endpoints;
    uint8_t interface_class;
    uint8_t interface_sub_class;
    uint8_t interface_protocol;
    uint8_t interface_string;
} __attribute__((packed));

struct EndpointDescriptor
{
    uint8_t length;
    uint8_t descriptor_type;  // = 5
    uint8_t endpoint_address; // 最上位ビットが1ならIN(デバイス→ホスト)
    uint8_t attributes;       // 3=Interrupt
    uint16_t max_packet_size;
    uint8_t interval;
} __attribute__((packed));
} // namespace USB