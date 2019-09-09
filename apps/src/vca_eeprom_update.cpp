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

#include "vca_eeprom_update.h"
#include "vcactrl.h"
#include "helper_funcs.h"
#include <errno.h>
#include <vector>
#include <algorithm>
#include <boost/crc.hpp>
#include <stdexcept>

using namespace Printer;
using vca::GUID;
using vca::EepromEntry;
using vca::EepromFileHeader;
using vca::EepromFile;

#define MAX_EEPROM_ENTRIES 1024

// {A924C9B6-B1D0-4C4A-8215-314539495ED4}
static const GUID valid_guid = {0xa9, 0x24, 0xc9, 0xb6, 0xb1, 0xd0, 0x4c, 0x4a, 0x82, 0x15, 0x31, 0x45, 0x39, 0x49, 0x5e, 0xd4};

static const unsigned min_file_size = sizeof(EepromFileHeader) + sizeof(GUID);

EepromFile::EepromFile(std::string file_name)
{
	m_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	m_file.open(file_name.c_str(), std::ifstream::binary);

	m_header.file_format_version.version = 0;
	m_header.file_version.version = 0;
	m_header.eeprom_entries_number = 0;

	if(!validate_file_len()) {
		throw std::runtime_error("EEPROM file size invalid!");
	}

	if (m_file.is_open()) {
		if (!validate_guid()) {
			LOG_ERROR("Invalid EEPROM file. Can not find required GUID!\nEEPROM update will be aborted!\n");
			m_file.close();
		}

		if (!check_crc()) {
			LOG_ERROR("Invalid EEPROM file. Bad CRC!\nEEPROM update will be aborted!\n");
			m_file.close();
		}

		if (m_file) {
			m_file.seekg(sizeof(GUID), m_file.beg);
			m_file.read((char*)&m_header, sizeof(EepromFileHeader));
		} else {
			LOG_ERROR("Invalid EEPROM file format. Cannot read header of eeprom file!\n");
		}

		if (m_file) {
			if (m_header.eeprom_entries_number > MAX_EEPROM_ENTRIES)
				throw std::runtime_error("To many EEPROM entires defined in file!");
			m_eeprom_entries.resize(m_header.eeprom_entries_number);
			m_file.read((char*)&m_eeprom_entries.front(), m_header.eeprom_entries_number * sizeof(EepromEntry));
		} else {
			LOG_ERROR("Invalid EEPROM file format. Cannot read eeprom table!\n");
		}
	} else {
		LOG_ERROR("Cannot open provided EEPROM file!\n");
	}
}

EepromFile::~EepromFile()
{
	if(m_file.is_open())
		m_file.close();
}

bool EepromFile::is_open()
{
	return m_file.is_open();
}

bool EepromFile::validate_guid()
{
	GUID file_guid;
	m_file.seekg(0, m_file.beg);
	m_file.read((char*)&file_guid, sizeof(GUID));

	if (!std::equal(file_guid,file_guid + sizeof(GUID), valid_guid)) {
		return false;
	}
	return true;
}

bool EepromFile::validate_file_len()
{
	m_file.seekg(0, m_file.end);
	unsigned file_size = m_file.tellg();
	return file_size >= min_file_size;
}

uint32_t EepromFile::get_file_crc()
{
	uint32_t crc = 0;
	m_file.seekg(-sizeof(crc) ,m_file.end);
	m_file.read(reinterpret_cast<char*>(&crc) ,sizeof(crc));
	return crc;
}

bool EepromFile::check_crc()
{
	uint32_t file_crc = get_file_crc();
	boost::crc_32_type crc;

	m_file.seekg(-sizeof(crc),m_file.end);
	unsigned f_size = m_file.tellg();
	m_file.seekg(m_file.beg);

	std::vector<char> file_data;
	file_data.resize(f_size);
	m_file.read(&file_data.front(), f_size);
	crc.process_bytes(&file_data.front(),file_data.size() );
	return file_crc == crc.checksum();
}

std::vector<EepromEntry> EepromFile::get_entries(
	bool (*predicate)(EepromEntry, unsigned), const unsigned card_id)
{
	std::vector<EepromEntry> entries;
	for (std::vector<EepromEntry>::const_iterator it = m_eeprom_entries.begin(); it != m_eeprom_entries.end(); ++it) {
		if (predicate(*it, card_id))
			entries.push_back(*it);
	}
	return entries;
}

vca::eeprom_binaries EepromFile::get_binary_eeproms(EepromEntry const& entry)
{
	std::vector<char> first_eeprom;
	first_eeprom.resize(entry.first_eeprom_size);
	m_file.seekg(entry.first_eeprom_offset,m_file.beg);
	m_file.read(&first_eeprom.front(), entry.first_eeprom_size);

	std::vector<char> second_eeprom;
	second_eeprom.resize(entry.second_eeprom_size);
	m_file.seekg(entry.second_eeprom_offset,m_file.beg);
	m_file.read(&second_eeprom.front(), entry.second_eeprom_size);

	return std::make_pair(first_eeprom, second_eeprom);
}
