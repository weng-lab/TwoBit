/*
 Copyright 2014 Arjan van der Velde, vandervelde.ag [at] gmail

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "TwoBitFile.hpp"
#include "TwoBit/objects/TwoBitSequence.hpp"

#include <njhcpp/utils/utils.hpp>
#include <algorithm>

namespace TwoBit
{

TwoBitFile::TwoBitFile(const bfs::path& filename) :
		swapped_(false), magic_(0), version_(0), sequenceCount_(0), reserved_(
				0), filename_(filename)
{
	file_.open(filename_.string(), std::ios::in | std::ios::binary);
	try {
		readTwoBitHeader();
		createSequenceMeta();
		for (std::pair<const std::string, TwoBitSequenceMeta>& meta : sequences_) {
			populateSequenceMeta(meta.second);
		}
	} catch (Exception& e) {
		file_.close();
		throw(e);
	}
	file_.close();
}

TwoBitFile::TwoBitFile(const TwoBitFile& other) : TwoBitFile(other.getFilename()){}

void TwoBitFile::readTwoBitHeader()
{
	// Read first 16 bytes of 2-bit file to get things going

	if (file_.read(reinterpret_cast<char*>(&magic_), 4)) {
		if (magic_ == MAGIC_NUMBER) {
			swapped_ = false;
		} else if (magic_ == REVERSE_MAGIC_NUMBER) {
			swapped_ = true;
		} else {
			throw Exception(__PRETTY_FUNCTION__, "Invalid magic number. Bad 2-bit file.");
		}
	} else {
		throw Exception(__PRETTY_FUNCTION__, "Error reading file.");
	}

	version_ = nextInt();		// always zero
	sequenceCount_ = nextInt();	// number of sequences
	reserved_ = nextInt();		// always zero

	// integrity check
	if (VERSION != version_) {
		throw Exception(__PRETTY_FUNCTION__, "Unexpected version number. Bad 2-bit file.");
	}
	if (RESERVED != reserved_) {
		throw Exception(__PRETTY_FUNCTION__, "Unexpected data. Bad 2-bit file.");
	}
}

void TwoBitFile::createSequenceMeta()
{
	// create SequenceMeta objects from name and offset

	uint32_t seqNameLen, offset;
	char seqName[SEQNAME_MAX_LEN];
	std::string seqNameStr; // name as std::string

	for (uint32_t i = 0; i < sequenceCount_; ++i) {
		seqNameLen = nextChar(); // length
		file_.read(seqName, seqNameLen); // sequence name
		seqNameStr = std::string(seqName, seqNameLen);
		offset = nextInt(); // offset

		// add meta data.
		sequences_.emplace(seqNameStr,
				TwoBitSequenceMeta(seqNameStr, offset, filename_.string(), swapped_));
		sequenceNames_.push_back(seqNameStr);
	}
}

void TwoBitFile::populateSequenceMeta(TwoBitSequenceMeta& meta)
{
	// seek to offset and read actual sequence meta data.

	file_.seekg(meta.offset_);
	meta.dnaSize_ = nextInt(); // length of sequence
	meta.dnaBytes_ = meta.dnaSize_ / 4 + (meta.dnaSize_ % 4 > 0);

	// read nRegions.
	readRegions(meta.nRegions); // N-regions
	readRegions(meta.mRegions); // mask regions

	// check. this number should be zero as per the spec.
	if (0 != nextInt()) {
		throw Exception(__PRETTY_FUNCTION__, "Unexpected data. Bad 2-bit file.");
	}

	// store start of packed data
	meta.packedPos_ = file_.tellg();

}

void TwoBitFile::readRegions(std::vector<TwoBitSequenceMeta::Region>& out)
{
	uint32_t count;
	std::vector<uint32_t> starts;
	std::vector<uint32_t> lengths;

	count = nextInt();
	for (uint32_t i = 0; i < count; ++i) {
		// read starts of regions
		starts.emplace_back(nextInt());
	}
	for (uint32_t i = 0; i < count; ++i) {
		// read lengths of regions
		lengths.emplace_back(nextInt());
	}

	// transform into a usable structure.
	// {(0, 0), (start, 1), (end, -1), (start, 1), (end, -1), ...}
	out.clear();
	out.reserve(count * 2);
	out.emplace_back(0, 0); // filler, makes logic a little easier
	for (uint32_t i = 0; i < count; ++i) {
		out.emplace_back(starts[i], 1);
		out.emplace_back(starts[i] + lengths[i], -1);
	}
	std::sort(out.begin() + 1, out.end(),
			[](const TwoBitSequenceMeta::Region& a, const TwoBitSequenceMeta::Region& b)
			{
				return a.pos_ < b.pos_;
			});
}

std::unique_ptr<TwoBitSequence> TwoBitFile::operator[](const std::string& s) const
{
	auto iter = sequences_.find(s);
	if (sequences_.end() != iter) {
		return std::make_unique<TwoBitSequence>(iter->second);
	} else {
		throw Exception(__PRETTY_FUNCTION__, "Unknown sequence '" + s + "'.");
	}
}

const std::vector<std::string>& TwoBitFile::sequenceNames() const
{
	return sequenceNames_;
}

bool TwoBitFile::hasSequenceName(const std::string & seqName) const{
	return njh::in(seqName, sequenceNames_);
}

std::unordered_map<std::string, uint32_t> TwoBitFile::getSeqLens() const{
	std::unordered_map<std::string, uint32_t> ret;
	for(const auto & meta : sequences_){
		ret[meta.first] = meta.second.dnaSize_;
	}
	return ret;
}

const uint32_t TwoBitFile::size() const
{
	return sequences_.size();
}

bfs::path TwoBitFile::getFilename() const{
	return filename_;
}

} // namespace TwoBit

