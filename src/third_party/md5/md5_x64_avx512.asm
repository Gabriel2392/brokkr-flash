; Copyright (c) 2026 Gabriel2392
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

.const
ALIGN 16
md5_k4 LABEL DWORD
    DWORD 0d76aa478h, 0d76aa478h, 0d76aa478h, 0d76aa478h
    DWORD 0e8c7b756h, 0e8c7b756h, 0e8c7b756h, 0e8c7b756h
    DWORD 0242070dbh, 0242070dbh, 0242070dbh, 0242070dbh
    DWORD 0c1bdceeeh, 0c1bdceeeh, 0c1bdceeeh, 0c1bdceeeh
    DWORD 0f57c0fafh, 0f57c0fafh, 0f57c0fafh, 0f57c0fafh
    DWORD 04787c62ah, 04787c62ah, 04787c62ah, 04787c62ah
    DWORD 0a8304613h, 0a8304613h, 0a8304613h, 0a8304613h
    DWORD 0fd469501h, 0fd469501h, 0fd469501h, 0fd469501h
    DWORD 0698098d8h, 0698098d8h, 0698098d8h, 0698098d8h
    DWORD 08b44f7afh, 08b44f7afh, 08b44f7afh, 08b44f7afh
    DWORD 0ffff5bb1h, 0ffff5bb1h, 0ffff5bb1h, 0ffff5bb1h
    DWORD 0895cd7beh, 0895cd7beh, 0895cd7beh, 0895cd7beh
    DWORD 06b901122h, 06b901122h, 06b901122h, 06b901122h
    DWORD 0fd987193h, 0fd987193h, 0fd987193h, 0fd987193h
    DWORD 0a679438eh, 0a679438eh, 0a679438eh, 0a679438eh
    DWORD 049b40821h, 049b40821h, 049b40821h, 049b40821h
    DWORD 0f61e2562h, 0f61e2562h, 0f61e2562h, 0f61e2562h
    DWORD 0c040b340h, 0c040b340h, 0c040b340h, 0c040b340h
    DWORD 0265e5a51h, 0265e5a51h, 0265e5a51h, 0265e5a51h
    DWORD 0e9b6c7aah, 0e9b6c7aah, 0e9b6c7aah, 0e9b6c7aah
    DWORD 0d62f105dh, 0d62f105dh, 0d62f105dh, 0d62f105dh
    DWORD 002441453h, 002441453h, 002441453h, 002441453h
    DWORD 0d8a1e681h, 0d8a1e681h, 0d8a1e681h, 0d8a1e681h
    DWORD 0e7d3fbc8h, 0e7d3fbc8h, 0e7d3fbc8h, 0e7d3fbc8h
    DWORD 021e1cde6h, 021e1cde6h, 021e1cde6h, 021e1cde6h
    DWORD 0c33707d6h, 0c33707d6h, 0c33707d6h, 0c33707d6h
    DWORD 0f4d50d87h, 0f4d50d87h, 0f4d50d87h, 0f4d50d87h
    DWORD 0455a14edh, 0455a14edh, 0455a14edh, 0455a14edh
    DWORD 0a9e3e905h, 0a9e3e905h, 0a9e3e905h, 0a9e3e905h
    DWORD 0fcefa3f8h, 0fcefa3f8h, 0fcefa3f8h, 0fcefa3f8h
    DWORD 0676f02d9h, 0676f02d9h, 0676f02d9h, 0676f02d9h
    DWORD 08d2a4c8ah, 08d2a4c8ah, 08d2a4c8ah, 08d2a4c8ah
    DWORD 0fffa3942h, 0fffa3942h, 0fffa3942h, 0fffa3942h
    DWORD 08771f681h, 08771f681h, 08771f681h, 08771f681h
    DWORD 06d9d6122h, 06d9d6122h, 06d9d6122h, 06d9d6122h
    DWORD 0fde5380ch, 0fde5380ch, 0fde5380ch, 0fde5380ch
    DWORD 0a4beea44h, 0a4beea44h, 0a4beea44h, 0a4beea44h
    DWORD 04bdecfa9h, 04bdecfa9h, 04bdecfa9h, 04bdecfa9h
    DWORD 0f6bb4b60h, 0f6bb4b60h, 0f6bb4b60h, 0f6bb4b60h
    DWORD 0bebfbc70h, 0bebfbc70h, 0bebfbc70h, 0bebfbc70h
    DWORD 0289b7ec6h, 0289b7ec6h, 0289b7ec6h, 0289b7ec6h
    DWORD 0eaa127fah, 0eaa127fah, 0eaa127fah, 0eaa127fah
    DWORD 0d4ef3085h, 0d4ef3085h, 0d4ef3085h, 0d4ef3085h
    DWORD 004881d05h, 004881d05h, 004881d05h, 004881d05h
    DWORD 0d9d4d039h, 0d9d4d039h, 0d9d4d039h, 0d9d4d039h
    DWORD 0e6db99e5h, 0e6db99e5h, 0e6db99e5h, 0e6db99e5h
    DWORD 01fa27cf8h, 01fa27cf8h, 01fa27cf8h, 01fa27cf8h
    DWORD 0c4ac5665h, 0c4ac5665h, 0c4ac5665h, 0c4ac5665h
    DWORD 0f4292244h, 0f4292244h, 0f4292244h, 0f4292244h
    DWORD 0432aff97h, 0432aff97h, 0432aff97h, 0432aff97h
    DWORD 0ab9423a7h, 0ab9423a7h, 0ab9423a7h, 0ab9423a7h
    DWORD 0fc93a039h, 0fc93a039h, 0fc93a039h, 0fc93a039h
    DWORD 0655b59c3h, 0655b59c3h, 0655b59c3h, 0655b59c3h
    DWORD 08f0ccc92h, 08f0ccc92h, 08f0ccc92h, 08f0ccc92h
    DWORD 0ffeff47dh, 0ffeff47dh, 0ffeff47dh, 0ffeff47dh
    DWORD 085845dd1h, 085845dd1h, 085845dd1h, 085845dd1h
    DWORD 06fa87e4fh, 06fa87e4fh, 06fa87e4fh, 06fa87e4fh
    DWORD 0fe2ce6e0h, 0fe2ce6e0h, 0fe2ce6e0h, 0fe2ce6e0h
    DWORD 0a3014314h, 0a3014314h, 0a3014314h, 0a3014314h
    DWORD 04e0811a1h, 04e0811a1h, 04e0811a1h, 04e0811a1h
    DWORD 0f7537e82h, 0f7537e82h, 0f7537e82h, 0f7537e82h
    DWORD 0bd3af235h, 0bd3af235h, 0bd3af235h, 0bd3af235h
    DWORD 02ad7d2bbh, 02ad7d2bbh, 02ad7d2bbh, 02ad7d2bbh
    DWORD 0eb86d391h, 0eb86d391h, 0eb86d391h, 0eb86d391h

.code

md5_blocks_x64_avx512 PROC
    test r8, r8
    jz   done
    lea  r9, md5_k4
    vmovd xmm0, DWORD PTR [rcx]
    vmovd xmm1, DWORD PTR [rcx + 4]
    vmovd xmm2, DWORD PTR [rcx + 8]
    vmovd xmm3, DWORD PTR [rcx + 12]
block:
    vmovdqa64 xmm18, xmm0
    vmovdqa64 xmm19, xmm1
    vmovdqa64 xmm20, xmm2
    vmovdqa64 xmm21, xmm3
    vmovd xmm16, DWORD PTR [rdx + 0]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 0]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 226
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 7
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 4]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 16]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 226
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 12
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 8]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 32]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 226
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 17
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 12]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 48]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 226
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 22
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 16]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 64]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 226
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 7
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 20]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 80]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 226
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 12
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 24]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 96]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 226
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 17
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 28]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 112]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 226
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 22
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 32]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 128]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 226
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 7
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 36]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 144]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 226
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 12
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 40]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 160]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 226
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 17
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 44]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 176]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 226
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 22
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 48]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 192]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 226
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 7
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 52]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 208]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 226
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 12
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 56]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 224]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 226
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 17
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 60]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 240]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 226
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 22
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 4]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 256]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 216
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 5
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 24]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 272]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 216
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 9
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 44]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 288]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 216
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 14
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 0]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 304]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 216
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 20
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 20]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 320]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 216
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 5
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 40]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 336]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 216
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 9
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 60]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 352]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 216
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 14
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 16]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 368]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 216
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 20
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 36]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 384]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 216
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 5
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 56]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 400]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 216
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 9
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 12]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 416]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 216
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 14
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 32]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 432]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 216
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 20
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 52]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 448]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 216
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 5
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 8]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 464]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 216
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 9
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 28]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 480]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 216
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 14
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 48]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 496]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 216
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 20
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 20]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 512]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 150
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 4
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 32]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 528]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 150
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 11
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 44]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 544]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 150
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 16
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 56]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 560]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 150
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 23
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 4]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 576]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 150
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 4
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 16]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 592]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 150
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 11
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 28]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 608]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 150
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 16
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 40]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 624]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 150
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 23
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 52]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 640]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 150
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 4
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 0]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 656]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 150
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 11
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 12]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 672]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 150
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 16
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 24]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 688]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 150
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 23
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 36]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 704]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 150
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 4
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 48]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 720]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 150
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 11
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 60]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 736]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 150
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 16
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 8]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 752]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 150
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 23
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 0]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 768]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 45
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 6
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 28]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 784]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 45
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 10
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 56]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 800]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 45
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 15
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 20]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 816]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 45
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 21
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 48]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 832]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 45
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 6
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 12]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 848]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 45
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 10
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 40]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 864]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 45
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 15
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 4]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 880]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 45
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 21
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 32]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 896]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 45
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 6
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 60]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 912]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 45
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 10
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 24]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 928]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 45
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 15
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 52]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 944]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 45
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 21
    vpaddd xmm1, xmm1, xmm2
    vmovd xmm16, DWORD PTR [rdx + 16]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 960]
    vpaddd xmm0, xmm0, xmm16
    vmovdqa64 xmm17, xmm2
    vpternlogd xmm17, xmm1, xmm3, 45
    vpaddd xmm0, xmm0, xmm17
    vprold xmm0, xmm0, 6
    vpaddd xmm0, xmm0, xmm1
    vmovd xmm16, DWORD PTR [rdx + 44]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 976]
    vpaddd xmm3, xmm3, xmm16
    vmovdqa64 xmm17, xmm1
    vpternlogd xmm17, xmm0, xmm2, 45
    vpaddd xmm3, xmm3, xmm17
    vprold xmm3, xmm3, 10
    vpaddd xmm3, xmm3, xmm0
    vmovd xmm16, DWORD PTR [rdx + 8]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 992]
    vpaddd xmm2, xmm2, xmm16
    vmovdqa64 xmm17, xmm0
    vpternlogd xmm17, xmm3, xmm1, 45
    vpaddd xmm2, xmm2, xmm17
    vprold xmm2, xmm2, 15
    vpaddd xmm2, xmm2, xmm3
    vmovd xmm16, DWORD PTR [rdx + 36]
    vpaddd xmm16, xmm16, XMMWORD PTR [r9 + 1008]
    vpaddd xmm1, xmm1, xmm16
    vmovdqa64 xmm17, xmm3
    vpternlogd xmm17, xmm2, xmm0, 45
    vpaddd xmm1, xmm1, xmm17
    vprold xmm1, xmm1, 21
    vpaddd xmm1, xmm1, xmm2
    vpaddd xmm0, xmm0, xmm18
    vpaddd xmm1, xmm1, xmm19
    vpaddd xmm2, xmm2, xmm20
    vpaddd xmm3, xmm3, xmm21
    add  rdx, 64
    dec  r8
    jnz  block
    vmovd DWORD PTR [rcx], xmm0
    vmovd DWORD PTR [rcx + 4], xmm1
    vmovd DWORD PTR [rcx + 8], xmm2
    vmovd DWORD PTR [rcx + 12], xmm3
    vzeroupper
done:
    ret
md5_blocks_x64_avx512 ENDP

END
