/*******************************************************************************
 * libretroshare/src/file_sharing: file_tree.cc                                *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2018  Retroshare Team <contact@retroshare.cc>                 *
 * Copyright (C) 2020  Gioacchino Mazzurco <gio@eigenlab.org>                  *
 * Copyright (C) 2020  Asociación Civil Altermundi <info@altermundi.net>       *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 ******************************************************************************/
#include <iomanip>

#include "util/radix64.h"
#include "util/rsbase64.h"
#include "util/rsdir.h"
#include "retroshare/rsfiles.h"
#include "file_sharing_defaults.h"
#include "filelist_io.h"
#include "serialiser/rstypeserializer.h"

void RsFileTree::DirData::serial_process(
            RsGenericSerializer::SerializeJob j,
            RsGenericSerializer::SerializeContext& ctx )
{
	RS_SERIAL_PROCESS(name);
	RS_SERIAL_PROCESS(subdirs);
	RS_SERIAL_PROCESS(subfiles);
}

void RsFileTree::FileData::serial_process(
            RsGenericSerializer::SerializeJob j,
            RsGenericSerializer::SerializeContext& ctx )
{
	RS_SERIAL_PROCESS(name);
	RS_SERIAL_PROCESS(size);
	RS_SERIAL_PROCESS(hash);
}

RsFileTree::RsFileTree()
    : mTotalFiles(0), mTotalSize(0)
{
    DirData dd;
    dd.name = "/";
    mDirs.push_back(dd);
}

/*static*/ std::tuple<std::unique_ptr<RsFileTree>, std::error_condition>
RsFileTree::fromBase64(const std::string& base64)
{
	const auto failure = [](std::error_condition ec)
	{ return std::make_tuple(nullptr, ec); };

	if(base64.empty()) return failure(std::errc::invalid_argument);

	std::error_condition ec;
	std::vector<uint8_t> mem;
	if( (ec = RsBase64::decode(base64, mem)) ) return failure(ec);

	RsGenericSerializer::SerializeContext ctx(
	            mem.data(), static_cast<uint32_t>(mem.size()),
	            RsSerializationFlags::INTEGER_VLQ );
	std::unique_ptr<RsFileTree> ft(new RsFileTree);
	ft->serial_process(
	            RsGenericSerializer::SerializeJob::DESERIALIZE, ctx);
	if(ctx.mOk) return std::make_tuple(std::move(ft), std::error_condition());

	return failure(std::errc::invalid_argument);
}

std::string RsFileTree::toBase64() const
{
	RsGenericSerializer::SerializeContext ctx;
	ctx.mFlags = RsSerializationFlags::INTEGER_VLQ;
	RsFileTree* ncThis = const_cast<RsFileTree*>(this);
	ncThis->serial_process(
	            RsGenericSerializer::SerializeJob::SIZE_ESTIMATE, ctx );

	std::vector<uint8_t> buf(ctx.mOffset);
	ctx.mSize = ctx.mOffset; ctx.mOffset = 0; ctx.mData = buf.data();

	ncThis->serial_process(
	            RsGenericSerializer::SerializeJob::SERIALIZE, ctx );
	std::string result;
	RsBase64::encode(ctx.mData, ctx.mSize, result, false, true);
	return result;
}

std::string RsFileTree::toRadix64() const
{
	unsigned char* buff = nullptr;
	uint32_t size = 0;
	serialise(buff, size);
	std::string res;
	Radix64::encode(buff,size,res);
	free(buff);
	return res;
}

std::unique_ptr<RsFileTree> RsFileTree::fromRadix64(
        const std::string& radix64_string )
{
	std::unique_ptr<RsFileTree> ft(new RsFileTree);
	std::vector<uint8_t> mem = Radix64::decode(radix64_string);
	if(ft->deserialise(mem.data(), static_cast<uint32_t>(mem.size())))
		return ft;
	return nullptr;
}

void RsFileTree::recurs_addTree( DirIndex parent, const RsFileTree& tree, DirIndex cdir)
{
    const auto& dd(tree.directoryData(cdir));

    for(uint32_t i=0;i<dd.subdirs.size();++i)
    {
        auto new_dir_index = addDirectory(parent,tree.directoryData(dd.subdirs[i]).name);
        recurs_addTree(new_dir_index,tree,dd.subdirs[i]);
    }

    for(uint32_t i=0;i<dd.subfiles.size();++i)
    {
        const auto& fd(tree.fileData(dd.subfiles[i]));
        addFile(parent,fd.name,fd.hash,fd.size);
    }
}

void RsFileTree::recurs_buildFileTree( RsFileTree& ft, uint32_t index, const DirDetails& dd, bool remote, bool remove_top_dirs )
{
	RsDbg() << __PRETTY_FUNCTION__ << " index: " << index << std::endl;
	if(ft.mDirs.size() <= index)
		ft.mDirs.resize(index+1) ;

	if(remove_top_dirs)
		ft.mDirs[index].name = RsDirUtil::getTopDir(dd.name) ;
	else
		ft.mDirs[index].name = dd.name ;

	ft.mDirs[index].subfiles.clear();
	ft.mDirs[index].subdirs.clear();

	DirDetails dd2 ;

	FileSearchFlags flags = remote ? RS_FILE_HINTS_REMOTE : RS_FILE_HINTS_LOCAL;

	for(uint32_t i=0;i<dd.children.size();++i)
		if(rsFiles->RequestDirDetails(dd.children[i].ref,dd2,flags))
		{
			if(dd.children[i].type == DIR_TYPE_FILE)
			{
				FileData f ;
				f.name = dd2.name ;
                f.size = dd2.size ;
				f.hash = dd2.hash ;

				ft.mDirs[index].subfiles.push_back(ft.mFiles.size()) ;
				ft.mFiles.push_back(f) ;

				ft.mTotalFiles++ ;
				ft.mTotalSize += f.size ;
			}
			else if(dd.children[i].type == DIR_TYPE_DIR)
			{
				ft.mDirs[index].subdirs.push_back(ft.mDirs.size());
				recurs_buildFileTree(ft,ft.mDirs.size(),dd2,remote,remove_top_dirs) ;
			}
			else
				std::cerr << "(EE) Unsupported DirDetails type." << std::endl;
		}
		else
			std::cerr << "(EE) Cannot request dir details for pointer " << dd.children[i].ref << std::endl;
}

const RsFileTree::DirData& RsFileTree::directoryData(DirIndex dir_handle) const
{
    assert(dir_handle < mDirs.size());

    return mDirs[dir_handle];
}

RsFileTree::DirIndex RsFileTree::addDirectory(DirIndex parent,const std::string& name)
{
    if(parent >= mDirs.size())
    {
        RsErr() << "Consistency error in RsFileTree::addDirectory. parent index " << parent << " does not exist.";
        return 0;
    }
    mDirs[parent].subdirs.push_back(mDirs.size());

    DirData dd;
    dd.name = name;
    mDirs.push_back(dd);

    return mDirs.size()-1;
}
RsFileTree::FileIndex RsFileTree::addFile(DirIndex parent,const std::string& name,const RsFileHash& hash,uint64_t size)
{
    if(parent >= mDirs.size())
    {
        RsErr() << "Consistency error in RsFileTree::addFile. parent index " << parent << " does not exist.";
        return 0;
    }
    FileData fd;
    fd.hash = hash;
    fd.size = size;
    fd.name = name;

    mDirs[parent].subfiles.push_back(mFiles.size());
    mFiles.push_back(fd);

    return mFiles.size()-1;
}
bool RsFileTree::updateFile(FileIndex file_index,const std::string& name,const RsFileHash& hash,uint64_t size)
{
    if(file_index >= mFiles.size())
    {
        RsErr() << "RsfileTree asked to update a file that is not referenced! name=" << name << ", hash=" << hash << ", size=" << hash << std::endl;
        return false;
    }
    auto& f(mFiles[file_index]);
    f.name = name;
    f.hash = hash;
    f.size = size;

    return true;
}
void RsFileTree::addFileTree(DirIndex parent,const RsFileTree& tree)
{
    recurs_addTree(parent,tree,tree.root());
}

bool RsFileTree::removeFile(FileIndex index_to_remove,DirIndex parent_index)
{
    assert(parent_index < mDirs.size());
    assert(index_to_remove < mFiles.size());
    bool found = false;

    for(uint32_t i=0;i<mDirs[parent_index].subfiles.size();++i)
        if(mDirs[parent_index].subfiles[i] == index_to_remove)		// costs a little more, but cleans the tree of all occurences just in case.
        {
            mDirs[parent_index].subfiles[i] = mDirs[parent_index].subfiles.back();
            mDirs[parent_index].subfiles.pop_back();
            found = true;
        }
    if(found)
        mFiles[index_to_remove] = FileData();

    return found;
}

bool RsFileTree::removeDirectory(DirIndex index_to_remove,DirIndex parent_index)
{
    assert(index_to_remove < mDirs.size());
    assert(parent_index < mDirs.size());

    // Lazy deletion: we do not remove the entire hierarchy but only the top dir (index_to_remove).
    // Later on, after possibly multiple edits, the tree can be properly cleaned of scories by calling
    // new_tree = RsFileTree::fromTreeCleaned(old_tree);

    for(uint32_t i=0;i<mDirs[parent_index].subdirs.size();++i)
        if(mDirs[parent_index].subdirs[i] == index_to_remove)
        {
            mDirs[parent_index].subdirs[i] = mDirs[parent_index].subdirs.back();
            mDirs[parent_index].subdirs.pop_back();

            return true;
        }

    return false;
}


const RsFileTree::FileData& RsFileTree::fileData(FileIndex file_handle) const
{
    assert(file_handle < mFiles.size());	// this should never happen!
    return mFiles[file_handle];
}
uint64_t RsFileTree::totalFileSize() const
{
    if(mTotalSize == 0)
        for(uint32_t i=0;i<mFiles.size();++i)
            mTotalSize += mFiles[i].size;

    return mTotalSize;
}

std::unique_ptr<RsFileTree> RsFileTree::fromFile(const std::string& name, uint64_t size, const RsFileHash&  hash)
{
    std::unique_ptr<RsFileTree>ft(new RsFileTree);

    FileData fd;
    fd.name = name;
    fd.hash = hash;
    fd.size = size;

    ft->mFiles.push_back(fd);
    ft->mDirs[0].subfiles.push_back(0);
    ft->mTotalFiles = 1;
    ft->mTotalSize = size;

    return ft;
}
std::unique_ptr<RsFileTree> RsFileTree::fromDirectory(const std::string& name)
{
    std::unique_ptr<RsFileTree>ft(new RsFileTree);

    DirData dd;
    dd.name = name;

    ft->mDirs[0].subdirs.push_back(1);
    ft->mDirs.push_back(dd);
    ft->mTotalFiles = 0;
    ft->mTotalSize = 0;

    return ft;
}
std::unique_ptr<RsFileTree> RsFileTree::fromDirDetails(
        const DirDetails& dd, bool remote ,bool remove_top_dirs )
{
    std::unique_ptr<RsFileTree>ft(new RsFileTree);
    if(dd.type == DIR_TYPE_FILE)
    {
        FileData fd;
        fd.name = dd.name; fd.hash = dd.hash; fd.size = dd.size;
        ft->mFiles.push_back(fd);
        ft->mDirs[0].subfiles.push_back(0);
        ft->mTotalFiles = 1;
        ft->mTotalSize = fd.size;
    }
    else
        recurs_buildFileTree(*ft, 0, dd, remote, remove_top_dirs );

    return ft;
}

std::unique_ptr<RsFileTree> RsFileTree::fromTreeCleaned(const RsFileTree &tree)
{
    std::unique_ptr<RsFileTree> ft(new RsFileTree);

    ft->recurs_addTree( ft->root(), tree, tree.root());
    return ft;
}

typedef FileListIO::read_error read_error ;

bool RsFileTree::deserialise(unsigned char *buffer,uint32_t buffer_size)
{
    uint32_t buffer_offset = 0 ;

    mTotalFiles = 0;
    mTotalSize = 0;

    try
    {
        // Read some header

        uint32_t version,n_dirs,n_files ;

        if(!FileListIO::readField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_DIRECTORY_VERSION,version)) throw read_error(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_DIRECTORY_VERSION) ;
        if(version != (uint32_t) FILE_LIST_IO_LOCAL_DIRECTORY_TREE_VERSION_0001) throw std::runtime_error("Wrong version number") ;

        if(!FileListIO::readField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_RAW_NUMBER,n_files)) throw read_error(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_RAW_NUMBER) ;
        if(!FileListIO::readField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_RAW_NUMBER,n_dirs))  throw read_error(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_RAW_NUMBER) ;

        // Write all file/dir entries

		mFiles.resize(n_files) ;
		mDirs.resize(n_dirs) ;

		unsigned char *node_section_data = NULL ;
		uint32_t node_section_size = 0 ;

        for(uint32_t i=0;i<mFiles.size() && buffer_offset < buffer_size;++i)	// only the 2nd condition really is needed. The first one ensures that the loop wont go forever.
        {
            uint32_t node_section_offset = 0 ;
#ifdef DEBUG_DIRECTORY_STORAGE
            std::cerr << "reading node " << i << ", offset " << buffer_offset << " : " << RsUtil::BinToHex(&buffer[buffer_offset],std::min((int)buffer_size - (int)buffer_offset,100)) << "..." << std::endl;
#endif

            if(FileListIO::readField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_FILE_ENTRY,node_section_data,node_section_size))
            {
                if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_NAME     ,mFiles[i].name   )) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_NAME     ) ;
                if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_SIZE     ,mFiles[i].size   )) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_SIZE     ) ;
                if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_SHA1_HASH,mFiles[i].hash   )) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_SHA1_HASH) ;

                mTotalFiles++ ;
                mTotalSize += mFiles[i].size ;
            }
            else
                throw read_error(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_FILE_ENTRY) ;
		}

        for(uint32_t i=0;i<mDirs.size() && buffer_offset < buffer_size;++i)
		{
            uint32_t node_section_offset = 0 ;

            if(FileListIO::readField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_DIR_ENTRY,node_section_data,node_section_size))
            {
				DirData& de(mDirs[i]) ;

                if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_NAME,de.name )) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_FILE_NAME      ) ;

                uint32_t n_subdirs = 0 ;
                uint32_t n_subfiles = 0 ;

                if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,n_subdirs)) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER) ;

                for(uint32_t j=0;j<n_subdirs;++j)
                {
                    uint32_t di = 0 ;
                    if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,di)) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER) ;
                    de.subdirs.push_back(di) ;
                }

                if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,n_subfiles)) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER) ;

                for(uint32_t j=0;j<n_subfiles;++j)
                {
                    uint32_t fi = 0 ;
                    if(!FileListIO::readField(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,fi)) throw read_error(node_section_data,node_section_size,node_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER) ;
                    de.subfiles.push_back(fi) ;
                }
            }
            else
                throw read_error(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_DIR_ENTRY) ;
        }
		free(node_section_data) ;

        return true ;
    }
    catch(read_error& e)
    {
#ifdef DEBUG_DIRECTORY_STORAGE
        std::cerr << "Error while reading: " << e.what() << std::endl;
#endif
        return false;
    }
	return true ;
}

bool RsFileTree::serialise(unsigned char *& buffer,uint32_t& buffer_size) const
{
	buffer = 0 ;
    uint32_t buffer_offset = 0 ;

    unsigned char *tmp_section_data = (unsigned char*)rs_malloc(FL_BASE_TMP_SECTION_SIZE) ;

    if(!tmp_section_data)
        return false;

    uint32_t tmp_section_size = FL_BASE_TMP_SECTION_SIZE ;

    try
    {
        // Write some header

		if(!FileListIO::writeField(
		            buffer, buffer_size, buffer_offset,
		            FILE_LIST_IO_TAG_LOCAL_DIRECTORY_VERSION,
		            (uint32_t) FILE_LIST_IO_LOCAL_DIRECTORY_TREE_VERSION_0001 ) )
			throw std::runtime_error("Write error") ;
        if(!FileListIO::writeField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_RAW_NUMBER,(uint32_t) mFiles.size())) throw std::runtime_error("Write error") ;
        if(!FileListIO::writeField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_RAW_NUMBER,(uint32_t) mDirs.size())) throw std::runtime_error("Write error") ;

        // Write all file/dir entries

        for(uint32_t i=0;i<mFiles.size();++i)
		{
			const FileData& fe(mFiles[i]) ;

			uint32_t file_section_offset = 0 ;

			if(!FileListIO::writeField(tmp_section_data,tmp_section_size,file_section_offset,FILE_LIST_IO_TAG_FILE_NAME     ,fe.name        )) throw std::runtime_error("Write error") ;
			if(!FileListIO::writeField(tmp_section_data,tmp_section_size,file_section_offset,FILE_LIST_IO_TAG_FILE_SIZE     ,fe.size        )) throw std::runtime_error("Write error") ;
			if(!FileListIO::writeField(tmp_section_data,tmp_section_size,file_section_offset,FILE_LIST_IO_TAG_FILE_SHA1_HASH,fe.hash        )) throw std::runtime_error("Write error") ;

			if(!FileListIO::writeField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_FILE_ENTRY,tmp_section_data,file_section_offset)) throw std::runtime_error("Write error") ;
		}

        for(uint32_t i=0;i<mDirs.size();++i)
		{
			const DirData& de(mDirs[i]) ;

			uint32_t dir_section_offset = 0 ;

			if(!FileListIO::writeField(tmp_section_data,tmp_section_size,dir_section_offset,FILE_LIST_IO_TAG_FILE_NAME      ,de.name            )) throw std::runtime_error("Write error") ;
			if(!FileListIO::writeField(tmp_section_data,tmp_section_size,dir_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,(uint32_t)de.subdirs.size()))  throw std::runtime_error("Write error") ;

			for(uint32_t j=0;j<de.subdirs.size();++j)
				if(!FileListIO::writeField(tmp_section_data,tmp_section_size,dir_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,(uint32_t)de.subdirs[j]))  throw std::runtime_error("Write error") ;

			if(!FileListIO::writeField(tmp_section_data,tmp_section_size,dir_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,(uint32_t)de.subfiles.size())) throw std::runtime_error("Write error") ;

			for(uint32_t j=0;j<de.subfiles.size();++j)
				if(!FileListIO::writeField(tmp_section_data,tmp_section_size,dir_section_offset,FILE_LIST_IO_TAG_RAW_NUMBER,(uint32_t)de.subfiles[j])) throw std::runtime_error("Write error") ;

			if(!FileListIO::writeField(buffer,buffer_size,buffer_offset,FILE_LIST_IO_TAG_LOCAL_DIR_ENTRY,tmp_section_data,dir_section_offset))         throw std::runtime_error("Write error") ;
		}

        free(tmp_section_data) ;
		buffer_size = buffer_offset;

        return true ;
    }
    catch(std::exception& e)
    {
        std::cerr << "Error while writing: " << e.what() << std::endl;

        if(buffer != NULL)
            free(buffer) ;

        if(tmp_section_data != NULL)
			free(tmp_section_data) ;

        return false;
    }
}
