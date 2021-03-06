#include "Loader.h"
#include <mutex>
#include <fstream>
#include <iostream>

Loader::Loader()
{
	maxMemory = 80000000;
	usedMemory = 0;
}

void * Loader::Get(std::string guid)
{

	void* retPtr = nullptr;
	std::string filePath;
	int losByteOffset = 0, losByteSize = 0;

	loadLock.lock();
	if (registry.find(guid) != registry.end())	//guid already exists in registry
	{
		if (registry[guid].data == nullptr)	//Already being worked on by another thread and will be inserted soon
		{
			loadLock.unlock();
			return nullptr;
		}
		else
		{
			registry[guid].referenceCount++;
			loadLock.unlock();
			return reinterpret_cast<void*>(registry[guid].data);		//Asset already exists in registry and has a proper pointer. Return the address
		}
	}
	else	//Guid is not yet in registry
	{	//Find out if there's such a guid in our packages
		if (guid.substr(0, 3) == "los")			//Asset is in our .los custom package
		{
			FindOffsetCustom(guid, losByteOffset, losByteSize);
			usedMemory += losByteSize;
			registry[guid].size = losByteSize;
		}
		else									//Asset is in a .zip package
			filePath = FindPathZip(guid);		//Add to usedMemory in function
		//Will crash deliberately if FindPath functions can't find the asset

		registry[guid].data = nullptr;		//If the asset exists but hasn't been loaded set it as nullptr temporarily so other threads can see that it's being loaded
	}
	loadLock.unlock();


	//If we've gotten this far, we can begin decompressing and processing the file
	std::string readerType = guid.substr(0, guid.find_first_of('.'));
	
	//Send filepath to correct package handler here
	if (readerType == "zip")
		registry[guid].data = decompressor.decompress("obj.zip", filePath);

	if (readerType == "los")
		registry[guid].data = losReader.read("textures.los", losByteOffset, losByteSize);

	registry[guid].referenceCount++;
	return reinterpret_cast<void*>(registry[guid].data);
}

void Loader::Free(std::string guid)
{
	freeLock.lock();
	if (registry.find(guid) != registry.end())
	{
		registry[guid].referenceCount--;
		cout << "Lowering reference count of " + guid + ". New reference count = " + to_string(registry[guid].referenceCount) << endl;
	}
	freeLock.unlock();
}

void Loader::Free()
{
	freeLock.lock();
	while (usedMemory > maxMemory)
	{
		bool found = false;
		for (auto i : registry)
		{
			if (!i.second.pinned && i.second.referenceCount <= 0)
			{
				cout << "Using too much memory. Removing guid: " + i.first << endl;
				delete[] i.second.data;
				usedMemory -= i.second.size;
				registry.erase(i.first);
				found = true;
				break; //Break the for loop so we can get another iterator, as the unordered_map has been altered
			}
		}
		if (!found)
		{
			cout << "Couldn't free enough memory. Shutting down." << endl;
			getchar();
			exit(0);
		}
	}
	freeLock.unlock();
}

void Loader::Pin(std::string guid)
{
	if (registry.find(guid) != registry.end())
	{
		registry[guid].pinned = true;
	}
}

void Loader::Unpin(std::string guid)
{
	if (registry.find(guid) != registry.end())
	{
		registry[guid].pinned = false;
	}
}

int Loader::GetSize(std::string guid)
{
	if (registry.find(guid) != registry.end())
	{
		return registry[guid].size;
	}
	else
	{
		std::cout << "Can't get size of " + guid + ". No such guid loaded in registry. Loading it now.";
		Get(guid);
		return(GetSize(guid));
	}
}


std::string Loader::FindPathZip(std::string guid)
{
	bool found = false;
	std::string line;
	std::ifstream fileTable;
	fileTable.open("fileTable.txt");

	while (!fileTable.eof() && fileTable.is_open())
	{
		std::getline(fileTable, line, ' ');		//Look for guid in table
		if (line == guid)						//If found
		{
			std::getline(fileTable, line, ' ');		//Get filepath of guid
			std::string size;
			std::getline(fileTable, size);
			usedMemory += stoi(size);
			registry[guid].size = stoi(size);
			fileTable.close();
			if (usedMemory > maxMemory)
			{
				registry[guid].pinned = true; Free(); registry[guid].pinned = false;	//Temporarily pin so we don't remove the entry we're trying to load in order to free up the space to load it
			}
			found = true;
		}
		else std::getline(fileTable, line);		//go to next line
	}

	if (!found)		//File not found in lookup table
	{
		
		std::cout << guid << " is not a valid GUID. No such file found in fileTable.txt" << std::endl;
		getchar();
		exit(0);		//Exit program because of stupid
	}
	return line;
}

void Loader::FindOffsetCustom(std::string guid, int& offset, int& size)
{
	bool found = false;
	std::string line;

	std::ifstream file;
	file.open("textures.los");
	std::string filecount; std::getline(file, filecount);//Get top line of file which says how many files there are (and how many lines left there are in the header)
	int files = stoi(filecount);
	for (int i = 0; i < files; i++)
	{
		std::getline(file, line, ' ');
		if (guid == line)
		{
			std::getline(file, line, ' ');
			offset = stoi(line);
			std::getline(file, line);
			size = stoi(line);
			usedMemory += size;
			registry[guid].size = size;
			if (usedMemory > maxMemory)
			{
				registry[guid].pinned = true; Free(); registry[guid].pinned = false;	//Temporarily pin so we don't remove the entry we're trying to load in order to free up the space to load it
			}
			file.close();
			return;
		}
		else std::getline(file, line);		//go to next line

	}


	//File not found in lookup table
	std::cout << guid << " is not a valid GUID. No such file found in textures.los" << std::endl;
	getchar();
	exit(0);		//Exit program because of stupid
}
