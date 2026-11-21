#include <vector>
#include <stdint.h>
#include <stdio.h>

// Reads the whole file into `out`. Returns false if it cannot be opened or
// read completely. Shared by the data tools that pack loose files.
static bool read_whole_file(const char* path, std::vector<uint8_t>& out) {
	FILE* fh = fopen(path, "rb");
	if (!fh)
		return false;

	bool ok = fseek(fh, 0, SEEK_END) == 0;
	const long len = ok ? ftell(fh) : -1;
	ok = ok && len >= 0 && fseek(fh, 0, SEEK_SET) == 0;
	if (ok) {
		out.resize((size_t)len);
		ok = len == 0 || fread(out.data(), 1, (size_t)len, fh) == (size_t)len;
	}
	fclose(fh);
	return ok;
}

static int create_path(const char* out_file_path) {

	char* tmp = new char[strlen(out_file_path) + 1];
	memset(tmp, 0, strlen(out_file_path) + 1);

	const char* sep = out_file_path;
	const char* prev_sep = out_file_path;
	sep = strchr(prev_sep, PATH_SEPARATOR_AS_CHAR);
	while (sep) {

		// skip multiple path separators
		if (sep == prev_sep) {
			prev_sep++;
			sep = strchr(prev_sep, PATH_SEPARATOR_AS_CHAR);
			continue;
		}

		strncat(tmp, prev_sep, sep - prev_sep + 1);
		// if dir does not exist and failed to be created
		if (!gos_FileExists(tmp) && !CreateDirectory(tmp, NULL))
		{
			DWORD err = GetLastError();
			if (err == ERROR_ALREADY_EXISTS) {
				SPEW(("DBG", "Failed to create directory %s, error code: %d - directory already exists\n", tmp, err));
			}
			else {
				PAUSE(("Failed to create directory %s, error code: %d\n", tmp, err));
				delete[] tmp;
				return -1;
			}
		}
		prev_sep = sep + 1;
		sep = strchr(prev_sep, PATH_SEPARATOR_AS_CHAR);
	}

	//strncat(tmp, prev_sep, sep - prev_sep + 1);
	strcat(tmp, prev_sep);

	// if dir does not exist and failed to be created
	if (!gos_FileExists(tmp) && !CreateDirectory(tmp, NULL))
	{
		DWORD err = GetLastError();
		if (err == ERROR_ALREADY_EXISTS) {
			SPEW(("DBG", "Failed to create directory %s, error code: %d - directory already exists\n", tmp, err));
		}
		else {
			PAUSE(("Failed to create directory %s, error code: %d\n", tmp, err));
			delete[] tmp;
			return -1;
		}
	}

	delete[] tmp;
	return 0;
}
