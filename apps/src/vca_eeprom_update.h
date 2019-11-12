/*
* Intel VCA Software Stack (VCASS)
*
* Copyright(c) 2016-2017 Intel Corporation.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* The full GNU General Public License is included in this distribution in
* the file called "COPYING".
*
* Intel VCA User Space Tools.
*/

#ifndef _VCA_EEPROM_H_
#define _VCA_EEPROM_H_

#include <stdio.h>
#include <fstream>
#include <string>
#include <vector>
#include <stdint.h>

namespace vca
{
	typedef std::pair<uint32_t, uint32_t> crc_pair;

	typedef struct
	{
		crc_pair crcs;
		const char* version;
	} eeprom_config_info;

	static const eeprom_config_info known_eeproms[] = {
		//               8749/8733    8717/8713
		{std::make_pair(0x6796f2a7, 0x0bfca75b), "1.23"},
		{std::make_pair(0xA172AB39, 0x1EF8459C), "1.25"},
		{std::make_pair(0xa407d63e, 0x78d7d1f2), "2.1"},
		{std::make_pair(0xD3D967CB, 0xCEAEE1C3), "2.2"},
		{std::make_pair(0x5D7DF7F5, 0xCEAEE1C3), "2.3"},
		{std::make_pair(0x17F654AD, 0xCEAEE1C3), "2.4"},
		{std::make_pair(0xB0E8290F, 0x10499C8D), "2.5"},
		{std::make_pair(0xB0E8290F, 0x10499C8D), "2.6"},
		{std::make_pair(0xFFC3AFE9, 0x10B4B784), "2.7"},
		{std::make_pair(0x70747760, 0xE3CD6EA0), "2.8"},
		{std::make_pair(0x195555DB, 0x2A40E70B), "2.9"},
		{std::make_pair(0xC5A6EDD0, 0x29370C31), "2.10" },
		{std::make_pair(0xD726E8AF, 0x996EC62B), "2.11" },
		{std::make_pair(0x0BD550A4, 0x9A192D11), "2.12" },
		{{0x47B81C7F, 0x62117204}, "2.13" },
		// FPGA
		{{0xad29c0ce, 0}, "FPGA 1.0" },
		// VCAA
		{{0xBA66347B, 0}, "VCAC-A 1.0"},
		{{0xbc5a48da, 0}, "VCAC-A 1.1"},
		{{0x687d552e, 0}, "VCAC-A 1.2"},
		{{0x6cc483ef, 0}, "VCAC-A 1.3"},
		{{0xc247bc29, 0}, "VCAC-A 1.4, broken, please downgrade to version 1.3"},
		// VCGA
		{{0xB60C70FD, 0}, "VCAC-R 1.0"},
		{{0x89e39209, 0}, "VCAC-R 1.1"},
		{{0x393b7801, 0}, "VCAC-R 1.2"},
		{{0x4bb11a52, 0}, "VCAC-R 1.3"},
		{{0x3c5edc5e, 0}, "VCAC-R 1.4 (DMA disabled on nodes)" },
		{{0x0bac71ee, 0}, "VCAC-R 1.5.0 (DMA disabled on nodes)"},
		{{0x603abe83, 0}, "VCAC-R 1.5.1 (DMA disabled on nodes)"},
	};

	const size_t num_known_eeproms =
		sizeof(vca::known_eeproms) / sizeof(vca::known_eeproms[0]);

	typedef std::vector<char> eeprom_binary;
	typedef std::pair<eeprom_binary, eeprom_binary> eeprom_binaries;

	typedef uint8_t GUID[16];

	union Version
	{
		struct
		{
			uint16_t major;
			uint16_t minor;
		} _format;
		uint32_t version;
	};

	struct EepromFileHeader
	{
		union Version file_format_version;
		union Version file_version;
		uint32_t eeprom_entries_number;
	};

	struct EepromEntry
	{
		uint32_t card_gen;
		uint32_t fab_bitmap;
		uint32_t first_eeprom_offset;
		uint32_t first_eeprom_size;
		uint32_t second_eeprom_offset;
		uint32_t second_eeprom_size;
	};

	class EepromFile
	{
	private:
		std::ifstream m_file;
		EepromFileHeader m_header;
		std::vector<EepromEntry> m_eeprom_entries;

		uint32_t get_file_format();
		uint32_t get_file_version();
		uint32_t get_eeprom_pairs_number();
		uint32_t get_file_crc();

		bool validate_file_len();
		bool validate_guid();
		bool check_crc();


		EepromFile (EepromFile const&  eeprom_file);
		EepromFile& operator = (EepromFile const&);
	public:
		EepromFile (std::string file_name);
		~EepromFile ();

		bool is_open();
		bool validate_file_format();

		std::vector<EepromEntry> get_entries(bool(*predicate)(EepromEntry, unsigned), const unsigned card_id);
		vca::eeprom_binaries get_binary_eeproms(const EepromEntry &entry);
	};

} // namespace vca

#endif // _VCA_EEPROM_H_
