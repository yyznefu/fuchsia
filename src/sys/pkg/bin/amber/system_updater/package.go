// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"fmt"
	"io/ioutil"
	"os"
	"syscall"
	"syscall/zx/fdio"

	zxio "syscall/zx/io"

	"fidl/fuchsia/io"
)

// UpdatePackage represents a handle to the update package.
type UpdatePackage struct {
	dir *os.File
}

// NewUpdatePackage creates an UpdatePackage from a fidl interface
func NewUpdatePackage(proxy *io.DirectoryWithCtxInterface) (*UpdatePackage, error) {
	updateDir := fdio.NewDirectoryWithCtx((*zxio.DirectoryAdminWithCtxInterface)(proxy))
	updateDirFile := os.NewFile(uintptr(syscall.OpenFDIO(updateDir)), "update")
	return &UpdatePackage{dir: updateDirFile}, nil
}

// Open a file from the update package for reading
func (p *UpdatePackage) Open(path string) (*os.File, error) {
	dirFd := p.dir.Fd()
	fileFd, err := syscall.OpenAt(int(dirFd), path, os.O_RDONLY, 0)
	if err != nil {
		return nil, err
	}
	return os.NewFile(uintptr(fileFd), path), nil
}

// Returns the files in the update package.
func (p *UpdatePackage) ListFiles() ([]string, error) {
	// Readdirnames() leaves the file offset where it finishes, make sure to
	// reset it before exiting.
	names, err := p.dir.Readdirnames(0)
	if err != nil {
		// Best-effort attempt to reset the directory but don't let it clobber
		// the original error.
		_, _ = p.dir.Seek(0, 0)
		return nil, fmt.Errorf("Failed to read directory: %s", err)
	}

	_, err = p.dir.Seek(0, 0)
	if err != nil {
		return nil, fmt.Errorf("Failed to reset directory: %s", err)
	}

	return names, nil
}

// ReadFile opens and reads a whole file from the update package
func (p *UpdatePackage) ReadFile(path string) ([]byte, error) {
	f, err := p.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	b, err := ioutil.ReadAll(f)
	if err != nil {
		return nil, err
	}
	return b, nil
}

// Stat a file from the update package
func (p *UpdatePackage) Stat(path string) (os.FileInfo, error) {
	f, err := p.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return f.Stat()
}

// Merkleroot of the update package
func (p *UpdatePackage) Merkleroot() (string, error) {
	b, err := p.ReadFile("meta")
	if err != nil {
		return "", err
	}
	merkle := string(b)
	return merkle, nil
}

// Close the handle to the UpdatePackage.
func (p *UpdatePackage) Close() error {
	err := p.dir.Close()
	p.dir = nil
	return err
}
