// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lz4/lz4frame.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>
#include <fbl/unique_fd.h>
#include <fvm/fvm-lz4.h>
#include <fvm/fvm-sparse.h>

#include "format.h"

// The number of additional slices a partition will need to become zxcrypt'd.
// TODO(planders): Replace this with a value supplied by ulib/zxcrypt.
constexpr size_t kZxcryptExtraSlices = 1;

// A Container represents a method of storing multiple file system partitions in an
// FVM-recognizable format
class Container {
public:
    // Returns a Container representation of the FVM within the given |path|, starting at |offset|
    // bytes of length |length| bytes. Will return an error if the file does not exist or is not a
    // valid Container type, or if flags is not zero or a valid combination of fvm::sparse_flags_t.
    static zx_status_t Create(const char* path, off_t offset, off_t length, uint32_t flags,
                              fbl::unique_ptr<Container>* out);

    Container(const char* path, size_t slice_size, uint32_t flags);

    virtual ~Container();

    // Resets the Container state so we are ready to add a new set of partitions
    // Init must be called separately from the constructor, as it will overwrite data pertinent to
    // an existing Container.
    virtual zx_status_t Init() = 0;

    // Reports various information about the Container, e.g. number of partitions, and runs fsck on
    // all supported partitions (blobfs, minfs)
    virtual zx_status_t Verify() const = 0;

    // Commits the Container data to disk
    virtual zx_status_t Commit() = 0;

    // Returns the Container's specified slice size (in bytes)
    virtual size_t SliceSize() const = 0;

    // Given a path to a valid file system partition, adds that partition to the container
    virtual zx_status_t AddPartition(const char* path, const char* type_name) = 0;

    // Calculates the minimum disk size required to hold the unpacked contents of the container.
    virtual uint64_t CalculateDiskSize() const = 0;
protected:
    // Returns the minimum disk size necessary to store |slice_count| slices of size |slice_size_|
    // in an FVM.
    uint64_t CalculateDiskSizeForSlices(size_t slice_count) const;

    fbl::StringBuffer<PATH_MAX> path_;
    fbl::unique_fd fd_;
    size_t slice_size_;
    uint32_t flags_;
};

// Wrapper around FVM metadata which attempts to read existing metadata from disk, allows
// new partitions and slices to be allocated, and writes updated metadata back to disk.
class FvmInfo {
public:
    FvmInfo() : valid_(false), dirty_(false), metadata_size_(0), vpart_hint_(1), pslice_hint_(1) {}

    // Resets the metadata to default values.
    zx_status_t Reset(size_t disk_size, size_t slice_size);

    // Loads and validates metadata from disk. If invalid metadata is found a success status is
    // returned, but valid_ is marked false.
    zx_status_t Load(const fbl::unique_fd& fd, uint64_t disk_offset, uint64_t disk_size);

    // Validates the loaded metadata.
    zx_status_t Validate() const;

    // Grows in-memory metadata representation to the specified size.
    zx_status_t Grow(size_t metadata_size);

    // Grows in-memory metadata representation to account for |slice_count| additional slices.
    zx_status_t GrowForSlices(size_t slice_count);

    // Writes metadata to the partition described by |fd| of size |disk_size|, starting at offset
    // |disk_offset|.
    zx_status_t Write(const fbl::unique_fd& fd, size_t disk_offset, size_t disk_size);

    // Allocates new partition (in memory) with a single slice.
    zx_status_t AllocatePartition(const fvm::partition_descriptor_t* partition, uint8_t* guid,
                                  uint32_t* vpart_index);

    // Allocates new slice for given partition (in memory).
    zx_status_t AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice);

    // Helpers to grab reference to partition/slice from metadata
    zx_status_t GetPartition(size_t index, fvm::vpart_entry_t** out) const;
    zx_status_t GetSlice(size_t index, fvm::slice_entry_t** out) const;

    fvm::fvm_t* SuperBlock() const;
    size_t MetadataSize() const { return metadata_size_; }
    size_t DiskSize() const { return SuperBlock()->fvm_partition_size; }
    size_t SliceSize() const { return SuperBlock()->slice_size; }

    // Returns true if the in-memory metadata has been changed from the original values (i.e.
    // partitions/slices have been allocated since initialization).
    bool IsDirty() const { return dirty_; }

    // Returns true if metadata_ contains valid FVM metadata.
    bool IsValid() const { return valid_; }

    // Checks whether the metadata is valid, and immediately exits the process if it isn't.
    void CheckValid() const;

private:
    bool valid_;
    bool dirty_;
    size_t metadata_size_;
    uint32_t vpart_hint_;
    uint32_t pslice_hint_;
    fbl::unique_ptr<uint8_t[]> metadata_;
};

class FvmContainer final : public Container {
    typedef struct {
        uint32_t vpart_index;
        uint32_t pslice_start;
        uint32_t slice_count;
        fbl::unique_ptr<Format> format;
    } partition_info_t;

public:
    // Creates an FVM container at the given path, creating a new file if one does not already
    // exist. |offset| and |length| are provided to specify the offset (in bytes) and the length
    // (in bytes) of the FVM within the file. For a file that has not yet been created, these
    // should both be 0. For a file that exists, if not otherwise specified the offset should be 0
    // and the length should be the size of the file.
    static zx_status_t Create(const char* path, size_t slice_size, off_t offset, off_t length,
                              fbl::unique_ptr<FvmContainer>* out);
    FvmContainer(const char* path, size_t slice_size, off_t offset, off_t length);
    ~FvmContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;

    // Extends the FVM container to the specified length
    zx_status_t Extend(size_t length);
    size_t SliceSize() const final;
    zx_status_t AddPartition(const char* path, const char* type_name) final;

    uint64_t CalculateDiskSize() const final;

    // Returns the actual disk size.
    uint64_t GetDiskSize() const;
private:
    uint64_t disk_offset_;
    uint64_t disk_size_;
    fbl::Vector<partition_info_t> partitions_;
    FvmInfo info_;

    // Write the |part_index|th partition to disk
    zx_status_t WritePartition(unsigned part_index);
    // Write a partition's |extent_index|th extent to disk. |*pslice| is the starting pslice, and
    // is updated to reflect the latest written pslice.
    zx_status_t WriteExtent(unsigned extent_index, Format* format, uint32_t* pslice);
    // Write one data block of size |block_size| to disk at |block_offset| within pslice |pslice|
    zx_status_t WriteData(uint32_t pslice, uint32_t block_offset, size_t block_size, void* data);
};

class CompressionContext {
public:
    CompressionContext() {}
    ~CompressionContext() {}
    zx_status_t Setup(size_t max_len);
    zx_status_t Compress(const void* data, size_t length);
    zx_status_t Finish();

    const void* GetData() const { return data_.get(); }
    size_t GetLength() const { return offset_; }

private:
    void IncreaseOffset(size_t value) {
        offset_ += value;
        ZX_DEBUG_ASSERT(offset_ <= size_);
    }

    size_t GetRemaining() const {
        return size_ - offset_;
    }

    void* GetBuffer() const {
        return data_.get() + offset_;
    }

    void Reset(size_t size) {
        data_.reset(new uint8_t[size]);
        size_ = size;
        offset_ = 0;
    }

    LZ4F_compressionContext_t cctx_;
    fbl::unique_ptr<uint8_t[]> data_;
    size_t size_ = 0;
    size_t offset_ = 0;
};

typedef struct {
    fvm::partition_descriptor_t descriptor;
    fbl::Vector<fvm::extent_descriptor_t> extents;
    fbl::unique_ptr<Format> format;
} partition_info_t;

// Given a target path and partition data from a SparseReader, generates a full FVM image.
class SparsePaver {
public:
    // Creates a SparsePaver with the given attributes.
    static zx_status_t Create(const char* path, size_t slice_size, size_t disk_offset,
                              size_t disk_size, fbl::unique_ptr<SparsePaver>* out);

    // Allocates the partition and slices described by |partition| to info_, and writes out
    // corresponding data from |reader| to the FVM. |partition| will not be modified.
    zx_status_t AddPartition(const partition_info_t* partition, fvm::SparseReader* reader);

    // Commits the FVM image by writing the metadata to disk.
    zx_status_t Commit();
private:
    SparsePaver(size_t disk_offset, size_t disk_size) : disk_offset_(disk_offset),
                                                        disk_size_(disk_size) {}

    // Initializes the FVM metadata.
    zx_status_t Init(const char* path, size_t slice_size);

    // Allocates the extent described by |extent| to the partition at |vpart_index|, as well as
    // allocating its slices and persisting all associated data.
    zx_status_t AddExtent(uint32_t vpart_index, fvm::extent_descriptor_t* extent,
                          fvm::SparseReader* reader);

    // Writes the next slice out to disk, reading as many of |bytes_left| as possible from |reader|
    // and appending zeroes if necessary.
    zx_status_t WriteSlice(size_t* bytes_left, fvm::SparseReader* reader);

    FvmInfo info_;
    fbl::unique_fd fd_;
    size_t disk_offset_; // Offset into fd_ at which to create FVM.
    size_t disk_size_; // Number of bytes allocated for the FVM.
    size_t disk_ptr_; // Marks the current offset within the target image.
    fbl::unique_ptr<uint8_t[]> data_; // Buffer to hold data to be written to disk.
};

class SparseContainer final : public Container {
public:
    static zx_status_t Create(const char* path, size_t slice_size, uint32_t flags,
                              fbl::unique_ptr<SparseContainer>* out);
    SparseContainer(const char* path, uint64_t slice_size, uint32_t flags);
    ~SparseContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;

    // Unpacks the sparse container and "paves" it to |path|.
    zx_status_t Pave(const char* path, size_t disk_offset = 0, size_t disk_size = 0);
    size_t SliceSize() const final;
    size_t SliceCount() const;
    zx_status_t AddPartition(const char* path, const char* type_name) final;

    // Decompresses the contents of the sparse file (if they are compressed), and writes the output
    // to |path|.
    zx_status_t Decompress(const char* path);

    uint64_t CalculateDiskSize() const final;

    // Checks whether the container will fit within a disk of size |target_size| (in bytes).
    zx_status_t CheckDiskSize(uint64_t target_size) const;
private:
    bool valid_;
    bool dirty_;
    size_t disk_size_;
    size_t extent_size_;
    fvm::sparse_image_t image_;
    fbl::Vector<partition_info_t> partitions_;
    CompressionContext compression_;
    fbl::unique_ptr<fvm::SparseReader> reader_;

    zx_status_t AllocatePartition(fbl::unique_ptr<Format> format);
    zx_status_t AllocateExtent(uint32_t part_index, uint64_t slice_start, uint64_t slice_count,
                               uint64_t extent_length);

    zx_status_t PrepareWrite(size_t max_len);
    zx_status_t WriteData(const void* data, size_t length);
    zx_status_t CompleteWrite();
    void CheckValid() const;
};
