#include "stdafx.h"
#include "MipsElfFile.h"
#include "Core/Misc.h"
#include "Core/Common.h"
#include "Util/CRC.h"

MipsElfFile::MipsElfFile()
{
	platform = Mips.GetVersion();
	section = segment = -1;
	opened = false;
}

bool MipsElfFile::open(bool onlyCheck)
{
	opened = !onlyCheck;
	Global.symData.startModule(this);
	return true;
}

void MipsElfFile::close()
{
	if (isOpen())
		save();

	Global.symData.endModule(this);
}

u64 MipsElfFile::getVirtualAddress()
{
	if (segment != -1)
	{
		ElfSegment* seg = elf.getSegment(segment);
		ElfSection* sect = seg->getSection(section);
		size_t addr = seg->getVirtualAddress() + sect->getOffset();
		return addr+sectionOffset;
	}
	
	// segmentless sections don't have a virtual address
	Logger::queueError(Logger::Error,L"Not inside a mapped section");
	return -1;
}

u64 MipsElfFile::getPhysicalAddress()
{
	if (segment != -1)
	{
		ElfSegment* seg = elf.getSegment(segment);
		ElfSection* sect = seg->getSection(section);
		size_t addr = seg->getOffset() + sect->getOffset();
		return addr;
	}
	
	if (section != -1)
	{
		ElfSection* sect = elf.getSegmentlessSection(section);
		return sect->getOffset();
	}
	
	Logger::queueError(Logger::Error,L"Not inside a section");
	return -1;
}

bool MipsElfFile::seekVirtual(u64 virtualAddress)
{
	// search in segments
	for (size_t i = 0; i < elf.getSegmentCount(); i++)
	{
		ElfSegment* seg = elf.getSegment(i);
		size_t segStart = seg->getVirtualAddress();
		size_t segEnd = segStart+seg->getPhysSize();

		if (segStart <= virtualAddress && virtualAddress < segEnd)
		{
			// find section
			for (size_t l = 0; l < seg->getSectionCount(); l++)
			{
				ElfSection* sect = seg->getSection(l);
				size_t sectStart = segStart+sect->getOffset();
				size_t sectEnd = sectStart+sect->getSize();
				
				if (sectStart <= virtualAddress && virtualAddress < sectEnd)
				{
					segment = (int) i;
					section = (int) l;
					sectionOffset = (size_t) (virtualAddress-sectStart);
					return true;
				}
			}

			Logger::queueError(Logger::Error,L"Found segment, but no containing section");
			return false;
		}
	}

	// segmentless sections don't have a virtual address
	Logger::printError(Logger::Error,L"Couldn't find a mapped section");
	return false;
}

bool MipsElfFile::seekPhysical(u64 physicalAddress)
{
	// search in segments
	for (size_t i = 0; i < elf.getSegmentCount(); i++)
	{
		ElfSegment* seg = elf.getSegment(i);
		size_t segStart = seg->getOffset();
		size_t segEnd = segStart+seg->getPhysSize();

		if (segStart <= physicalAddress && physicalAddress < segEnd)
		{
			// find section
			for (size_t l = 0; l < seg->getSectionCount(); l++)
			{
				ElfSection* sect = seg->getSection(l);
				size_t sectStart = segStart+sect->getOffset();
				size_t sectEnd = sectStart+sect->getSize();
				
				if (sectStart <= physicalAddress && physicalAddress < sectEnd)
				{
					segment = (int) i;
					section = (int) l;
					sectionOffset = (size_t) (physicalAddress-sectStart);
					return true;
				}
			}

			Logger::queueError(Logger::Error,L"Found segment, but no containing section");
			return false;
		}
	}

	// search in segmentless sections
	for (size_t i = 0; i < elf.getSegmentlessSectionCount(); i++)
	{
		ElfSection* sect = elf.getSegmentlessSection(i);
		size_t sectStart = sect->getOffset();
		size_t sectEnd = sectStart+sect->getSize();
		
		if (sectStart <= physicalAddress && physicalAddress < sectEnd)
		{
			segment = -1;
			section = (int) i;
			sectionOffset = (size_t) (physicalAddress-sectStart);
			return true;
		}
	}

	segment = -1;
	section = -1;
	Logger::queueError(Logger::Error,L"Couldn't find a section");
	return false;
}

bool MipsElfFile::getModuleInfo(SymDataModuleInfo& info)
{
	info.crc32 = getCrc32(elf.getFileData().data(),elf.getFileData().size());
	return true;
}

bool MipsElfFile::write(void* data, size_t length)
{
	if (segment != -1)
	{
		ElfSegment* seg = elf.getSegment(segment);
		ElfSection* sect = seg->getSection(section);

		size_t pos = sect->getOffset()+sectionOffset;
		seg->writeToData(pos,data,length);
		sectionOffset += length;
		return true;
	}

	if (section != -1)
	{
		// TODO: segmentless sections
		return false;
	}

	Logger::printError(Logger::Error,L"Not inside a section");
	return false;
}

bool MipsElfFile::load(const std::wstring& fileName, const std::wstring& outputFileName)
{
	this->outputFileName = outputFileName;

	if (elf.load(fileName,true) == false)
	{
		Logger::printError(Logger::FatalError,L"Failed to load %s",fileName);
		return false;
	}

	if (elf.getType() == 0xFFA0)
	{
		Logger::printError(Logger::FatalError,L"Relocatable ELF %s not supported yet",fileName);
		return false;
	}

	if (elf.getType() != 2)
	{
		Logger::printError(Logger::FatalError,L"Unknown ELF %s type %d",fileName,elf.getType());
		return false;
	}

	return true;
}

bool MipsElfFile::setSection(const std::wstring& name)
{
	std::string utf8Name = convertWStringToUtf8(name);

	// look in segments
	for (size_t i = 0; i < elf.getSegmentCount(); i++)
	{
		ElfSegment* seg = elf.getSegment(i);
		int n = seg->findSection(utf8Name);
		if (n != -1)
		{
			segment = (int) i;
			section = n;
			return true;
		}
	}

	// look in stray sections
	int n = elf.findSegmentlessSection(utf8Name);
	if (n != -1)
	{
		segment = -1;
		section = n;
		return true;
	}

	Logger::queueError(Logger::Warning,L"Section %s not found",name);
	return false;
}

void MipsElfFile::save()
{
	elf.save(outputFileName);
}

//
// DirectiveLoadPspElf
//

DirectiveLoadMipsElf::DirectiveLoadMipsElf(ArgumentList& args)
{
	file = new MipsElfFile();

	if (args.size() == 2)
	{
		inputName = getFullPathName(args[0].text);
		outputName = getFullPathName(args[1].text);
		if (file->load(inputName,outputName) == false)
		{
			delete file;
			file = NULL;
			return;
		}
	} else {
		inputName = getFullPathName(args[0].text);
		if (file->load(inputName,inputName) == false)
		{
			delete file;
			file = NULL;
			return;
		}
	}

	g_fileManager->addFile(file);
}

bool DirectiveLoadMipsElf::Validate()
{
	Arch->NextSection();
	g_fileManager->openFile(file,true);
	return false;
}

void DirectiveLoadMipsElf::Encode()
{
	g_fileManager->openFile(file,false);
}

void DirectiveLoadMipsElf::writeTempData(TempData& tempData)
{
	if (outputName.empty())
	{
		tempData.writeLine(g_fileManager->getVirtualAddress(),formatString(L".loadelf \"%s\"",inputName));
	} else {
		tempData.writeLine(g_fileManager->getVirtualAddress(),formatString(L".loadelf \"%s\",\"%s\"",
			inputName,outputName));
	}
}
