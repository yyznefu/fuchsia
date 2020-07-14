// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
)

func TestExpand(t *testing.T) {
	// Create temporary work directory.
	parentDir, err := ioutil.TempDir("", "omaha-pkg-test-expand")
	if err != nil {
		t.Fatalf("Failed to create directory %s, %s", parentDir, err)
	}
	defer os.RemoveAll(parentDir)
	log.Printf("TestExpand working dir: %s", parentDir)

	_, expandDir, err := createAndExpandPackage(parentDir)
	if err != nil {
		t.Fatalf("Failed to create and expand package. %s", err)
	}

	// Make a "set" of all files we expect to see in the expand directory.
	expectedFiles := map[string]struct{}{
		"meta/contents": {},
		"meta/package":  {},
	}
	for _, item := range build.TestFiles {
		expectedFiles[item] = struct{}{}
	}

	expandedData := make(map[string]FileData)

	// Check that the contents of match expectations.
	filepath.Walk(expandDir,
		func(path string, info os.FileInfo, err error) error {
			if err != nil {
				log.Printf("Walk of expand directory failed with %s", err)
				return err
			}
			if !info.IsDir() {
				relativePath := strings.Replace(path, expandDir+"/", "", 1)
				expandedData[relativePath], err = ioutil.ReadFile(path)
				if err != nil {
					t.Fatalf("Could not read file %s. %s", path, err)
				}
				// Confirm all files we find are expected to be found
				if _, ok := expectedFiles[relativePath]; !ok {
					t.Fatalf("TestFiles does not contain %s.", relativePath)
				}
			}
			return nil
		})

	// Confirm we found the same number of files as we expected.
	if len(expectedFiles) != len(expandedData) {
		t.Fatalf("Expanded directory has %d files when %d are expected.",
			len(expandedData), len(expectedFiles))
	}

	// Compare file contents
	for key, val := range expandedData {
		valStr := strings.TrimSpace(string(val))
		// Skipping autogenerated file contents and "rand*" files as they do not have
		// consistent contents.
		if !strings.Contains(key, "rand") && key != "meta/contents" &&
			key != "meta/package" {
			if !strings.Contains(key, valStr) {
				t.Fatalf("File %s should have '%s' but has '%s'", key, key, valStr)
			}
		}
	}
}
