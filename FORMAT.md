# WoWs .pkg & .idx Format

## Introduction

The WoWs resources are packed into something similar to a `.zip` archive (WoTs and WoWPs actually use zip files).

Each archive is actually separated in two files:

* a `.pkg` containing the compressed files concatenated together. This file is in the `res_package/` directory.
* a `.idx` containing the index of the files contained in the `.pkg` and their metadata (name, path, type, etc). This file is located in the `./bin/<build_number>/idx/` directory.

## Disclaimer

This documentation has been created through reverse engineering, consequently errors and inaccuracies are not unlikely.

## Convention

A **byte/8 bits** is represented as follows:
```
+====+
| XX |
+====+
```

A **variable length field** (ex: strings) is represented as follows:

```
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
|           Field            |
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
```

The boundary between two fields is marked as follows:

```
...=++=...
    ||
...=++=...
```


## Misc


Integers (offset and size in particular) are `Big Endian`.

Strings seems limited to ASCII and are `\0` terminated.

## Index file


### General format

The index file is composed of 5 sections:

```
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| ^
|           Header           | } index header (number of files, pointers to sections, etc)
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| v

|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| ^
|       File metadata 1      | |
|----------------------------| |
|           [...]            | } metadata section (pointer to name, type, etc)
|----------------------------| |
|      File metadata Nfd     | |
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| v

|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| ^
|        File Name 1         | |
|----------------------------| |
|           [...]            | } file names (`\0` separated strings)
|----------------------------| |
|        File Name Nfd       | |
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| v

|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| ^
|   File `.pkg` pointers 1   | |
|----------------------------| |
|           [...]            | }  pkg pointers section in the `.pkg` file (offsets)
|----------------------------| |
|   File `.pkg` pointers Nf  | |
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| v

|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| ^
|           Footer           | } index footer (corresponding `.pkg` file name)
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~| V
```

### Header

#### Layout
```
+====+====+====+====++====+====+====+====++====+====+====+====++====+====+====+====+
| MA | MA | MA | MA || 00 | 00 | 00 | 02 || ID | ID | ID | ID || 40 | 00 | 00 | 00 |
+====+====+====+====++====+====+====+====++====+====+====+====++====+====+====+====+
|<----- magic ----->||<--- endianess --->||<------- id ------>||<--- unknown_2 --->|
|     32 bits       ||      32 bits      ||     32 bits       ||      32 bits      |

+====+====+====+====++====+====+====+====++====+====+====+====++====+====+====+====+
| FD | FD | FD | FD || FO | FO | FO | FO || 01 | 00 | 00 | 00 || 00 | 00 | 00 | 00 |
+====+====+====+====++====+====+====+====++====+====+====+====++====+====+====+====+
|<- file_dir_count >||<-- file_count --->||<-------------- unknown_3 ------------->|
|     32 bits       ||      32 bits      ||                64 bits                 |

+====+====+====+====+====+====+====+=====++=====+====+====+====+====+====+====+====+
| HS | HS | HS | HS | HS | HS | HS | HS  ||  OF | OF | OF | OF | OF | OF | OF | OF |
+====+====+====+====+====+====+====+=====++=====+====+====+====+====+====+====+====+
|<------------- header_size ------------>||<------- offset_idx_data_section ------>|
|                64 bits                 ||             64 bits                    |

+====+====+====+====+====+====+====+=====+
| OE | OE | OE | OE | OE | OE | OE | OE  |
+====+====+====+====+====+====+====+=====+
|<----- offset_idx_footer_section ------>|
|               64 bits                  |
```

#### Field descriptions

| Field                      |  size   | Description                                                                                                                                     |
|----------------------------|---------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| `magic`                    | 32 bits | Magic bytes, always "ISFP"                                                                                                                      |
| `endianess`                | 32 bits | Endianess marker, always 0x2000000 if LE                                                                                                        |
| `id`                       | 32 bits | Unsure, unique per index file, not referenced anywhere else                                                                                     |
| `unknown_2`                | 32 bits | Unknown, always 0x40, maybe some offset                                                                                                         |
| `file_dir_count`           | 32 bits | Number of files + directories (Nfd), also number of entries in the metadata section and the file names section                                  |
| `file_count`               | 32 bits | Number of files (Nf), also the number of entries in the file pkg pointers section                                                               |
| `unknown_3`                | 64 bits | Unknown, always '1', maybe the number of `.pkg` file the index file references (the format hints that it might be supported, but it's not used) |
| `header_size`              | 64 bits | Most likely the header size, always 40                                                                                                          |
| `offset_idx_data_section`  | 64 bits | Offset to the pkg data section, the offset is computed from `file_plus_dir_count` so `0x10` needs to be added                                   |
| `offset_idx_footer_section`| 64 bits | Offset to the footer section, the offset is computed from `file_plus_dir_count` so  `0x10` needs to be added                                    |

### File metadata

This section is repeated for each file and directory (`header->file_dir_count`).

#### Layout

```
+====+====+====+====+====+====+====+=====++=====+====+====+====+====+====+====+====+
| NS | NS | NS | NS | NS | NS | NS | NS  ||  OF | OF | OF | OF | OF | OF | OF | OF |
+====+====+====+====+====+====+====+=====++=====+====+====+====+====+====+====+====+
|<---------- file_name_size ------------>||<-------- offset_idx_file_name -------->|
|               64 bits                  ||              64 bits                   |

+====+====+====+====+====+====+====+=====++=====+====+====+====+====+====+====+====+
| UN | UN | UN | UN | UN | UN | UN | UN  ||  T2 | T2 | T2 | T2 | T2 | T2 | T2 | T2 |
+====+====+====+====+====+====+====+=====++=====+====+====+====+====+====+====+====+
|<----------------- id ----------------->||<------------- parent_id  ------------->|
|                64 bits                 ||                64 bits                 |

[...repeat...]
```

#### Field descriptions

| Field                  | Size    | Description                                                                               |
|------------------------|---------|-------------------------------------------------------------------------------------------|
| `file_name_size`       | 64 bits | Size of the file name string                                                              |
| `offset_idx_file_name` | 64 bits | Offset from the start of the current metadata record to the start of the file name string |
| `id`                   | 64 bits | Unique ID of the metadata record                                                          |
| `parent_id`            | 64 bits | ID of the potential parent record (in particular, a directory record)                     |

### File names section

This section is just `\0` separated list of strings:

#### Layout
```
+~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~+====+
|             file name string         | 00 |
+~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~+====+
[...repeat...]
```

### File ".pkg" pointers

This section  is repeated for each file (`header->file_count`).


#### Layout
```
+====+====+====+====+====+====+====+====++=====+====+====+====+====+====+====+====+
| UO | UO | UO | UO | UO | UO | UO | UO ||  UT | UT | UT | UT | UT | UT | UT | UT |
+====+====+====+====+====+====+====+====++=====+====+====+====+====+====+====+====+
|<----------- metadata_id ------------->||<------------- footer_id -------------->|
|               64 bits                 ||                64 bits                 |

+====+====+====+====+====+====+====+====++====+====+====+====++====+====+====+====+
| OF | OF | OF | OF | OF | OF | OF | OF || T1 | T1 | T1 | T1 || T2 | T2 | T2 | T2 |
+====+====+====+====+====+====+====+====++====+====+====+====++====+====+====+====+
|<--------- offset_pkg_data ----------->||<---- type_1 ----->||<----- type_2 ---->|
|               64 bits                 ||     32 bits       ||      32 bits      |

+====+====+====+====++====+====+====+====+====+====+====+====++====+====+====+====+
| OE | OE | OE | OE || ID | ID | ID | ID | ID | ID | ID | ID || 00 | 00 | 00 | 00 |
+====+====+====+====++====+====+====+====+====+====+====+====++====+====+====+====+
|<- size_pkg_data ->||<------------ id_pkg_data ------------>||<---- padding ---->|
|     32 bits       ||               64 bits                 ||      32 bits      |
[...repeat...]
```
#### Field descriptions

| Field             | Size    | Description                                                     |
|-------------------|---------|-----------------------------------------------------------------|
| `metadata_id`     | 64 bits | ID of the corresponding metadata entry                          |
| `footer_id`       | 64 bits | ID of the footer entry (only one entry possible in practice)    |
| `offset_pkg_data` | 64 bits | Offset to the compressed data from the start of the `.pkg` file |
| `type_1`          | 32 bits | Compression param 1 (`0x0` for uncompressed, `0x5` for deflate) |
| `type_2`          | 32 bits | Compression param 2 (`0x0` for uncompressed, `0x1` for deflate) |
| `size_pkg_data`   | 32 bits | Size of the compressed data section in the `.pkg` file          |
| `id_pkg_data`     | 64 bits | ID of the data section in the `.pkg` file                       |
| `padding`         | 32 bits | Always `0x00000000`                                             |

### Footer

#### Layout
```
+====+====+====+====+====+====+====+====++=====+====+====+====+====+====+====+====+
| UO | UO | UO | UO | UO | UO | UO | UO ||  U3 | U3 | U3 | U3 | U3 | U3 | U3 | U3 |
+====+====+====+====+====+====+====+====++=====+====+====+====+====+====+====+====+
|<--------- pkg_file_name_size -------->||<-------------- unknown_7 ------------->|
|               64 bits                 ||                64 bits                 |

+====+====+====+====+====+====+====+====++~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~...
| UT | UT | UT | UT | UT | UT | UT | UT ||             pkg_file_name_string
+====+====+====+====+====+====+====+====++~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~...
|<----------------- id ----------------->|
|                64 bits                 |
```

#### Field descriptions



| Field                | Size    | Description                                       |
|----------------------|---------|---------------------------------------------------|
| `pkg_file_name_size` | 64 bits | Size of the corresponding `.pkg` file name string |
| `unknown_7`          | 64 bits | unknown, looks like an ID                         |
| `id`                 | 64 bits | ID of the footer entry                            |

## PKG format

### PKG Entry

The `.pkg` format is rather simple, it's bunch of concatenated compressed (RFC 1951/Deflate) or raw/uncompressed data blobs (one for each file) separated by an ID.

#### Layout

```
+~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~+
|                                                                                 |
|                   Compressed (RFC 1951/Deflate) or Raw Data                     |
|                                                                                 |
+~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~+
+====+====+====+====++====+====+====+====+====+====+====+====++====+====+====+====+
| 00 | 00 | 00 | 00 || XX | XX | XX | XX | XX | XX | 00 | 00 || 00 | 00 | 00 | 00 |
+====+====+====+====++====+====+====+====+====+====+====+====++====+====+====+====+
|<--- padding_1 --->||<---------------- id ----------------->||<--- padding_2 --->|
|     32 bits       ||               64 bits                 ||      32 bits      |

[...repeat...]
```

#### Field descriptions

| Field             | Size    | Description         |
|-------------------|---------|---------------------|
| `padding_1`       | 32 bits | Always `0x00000000` |
| `id_pkg_data`     | 64 bits | ID of the data blob |
| `padding_2`       | 32 bits | Always `0x00000000` |
