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

.code

md5_blocks_x64 PROC FRAME
    push rbx
    .pushreg rbx
    .endprolog
    test r8, r8
    jz   done
    mov  eax, DWORD PTR [rcx]
    mov  r10d, DWORD PTR [rcx + 4]
    mov  r11d, DWORD PTR [rcx + 8]
    mov  QWORD PTR [rsp + 16], rcx
    mov  ecx, DWORD PTR [rcx + 12]
block:
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 0]
    xor  r9d, ecx
    add  eax, 0d76aa478h
    and  r9d, r10d
    xor  r9d, ecx
    add  eax, r9d
    rol  eax, 7
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 4]
    xor  r9d, r11d
    add  ecx, 0e8c7b756h
    and  r9d, eax
    xor  r9d, r11d
    add  ecx, r9d
    rol  ecx, 12
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 8]
    xor  r9d, r10d
    add  r11d, 0242070dbh
    and  r9d, ecx
    xor  r9d, r10d
    add  r11d, r9d
    rol  r11d, 17
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 12]
    xor  r9d, eax
    add  r10d, 0c1bdceeeh
    and  r9d, r11d
    xor  r9d, eax
    add  r10d, r9d
    rol  r10d, 22
    add  r10d, r11d
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 16]
    xor  r9d, ecx
    add  eax, 0f57c0fafh
    and  r9d, r10d
    xor  r9d, ecx
    add  eax, r9d
    rol  eax, 7
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 20]
    xor  r9d, r11d
    add  ecx, 04787c62ah
    and  r9d, eax
    xor  r9d, r11d
    add  ecx, r9d
    rol  ecx, 12
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 24]
    xor  r9d, r10d
    add  r11d, 0a8304613h
    and  r9d, ecx
    xor  r9d, r10d
    add  r11d, r9d
    rol  r11d, 17
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 28]
    xor  r9d, eax
    add  r10d, 0fd469501h
    and  r9d, r11d
    xor  r9d, eax
    add  r10d, r9d
    rol  r10d, 22
    add  r10d, r11d
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 32]
    xor  r9d, ecx
    add  eax, 0698098d8h
    and  r9d, r10d
    xor  r9d, ecx
    add  eax, r9d
    rol  eax, 7
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 36]
    xor  r9d, r11d
    add  ecx, 08b44f7afh
    and  r9d, eax
    xor  r9d, r11d
    add  ecx, r9d
    rol  ecx, 12
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 40]
    xor  r9d, r10d
    add  r11d, 0ffff5bb1h
    and  r9d, ecx
    xor  r9d, r10d
    add  r11d, r9d
    rol  r11d, 17
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 44]
    xor  r9d, eax
    add  r10d, 0895cd7beh
    and  r9d, r11d
    xor  r9d, eax
    add  r10d, r9d
    rol  r10d, 22
    add  r10d, r11d
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 48]
    xor  r9d, ecx
    add  eax, 06b901122h
    and  r9d, r10d
    xor  r9d, ecx
    add  eax, r9d
    rol  eax, 7
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 52]
    xor  r9d, r11d
    add  ecx, 0fd987193h
    and  r9d, eax
    xor  r9d, r11d
    add  ecx, r9d
    rol  ecx, 12
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 56]
    xor  r9d, r10d
    add  r11d, 0a679438eh
    and  r9d, ecx
    xor  r9d, r10d
    add  r11d, r9d
    rol  r11d, 17
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 60]
    xor  r9d, eax
    add  r10d, 049b40821h
    and  r9d, r11d
    xor  r9d, eax
    add  r10d, r9d
    rol  r10d, 22
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 4]
    not  r9d
    add  eax, 0f61e2562h
    and  r9d, r11d
    add  eax, r9d
    mov  r9d, ecx
    and  r9d, r10d
    add  eax, r9d
    rol  eax, 5
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 24]
    not  r9d
    add  ecx, 0c040b340h
    and  r9d, r10d
    add  ecx, r9d
    mov  r9d, r11d
    and  r9d, eax
    add  ecx, r9d
    rol  ecx, 9
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 44]
    not  r9d
    add  r11d, 0265e5a51h
    and  r9d, eax
    add  r11d, r9d
    mov  r9d, r10d
    and  r9d, ecx
    add  r11d, r9d
    rol  r11d, 14
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 0]
    not  r9d
    add  r10d, 0e9b6c7aah
    and  r9d, ecx
    add  r10d, r9d
    mov  r9d, eax
    and  r9d, r11d
    add  r10d, r9d
    rol  r10d, 20
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 20]
    not  r9d
    add  eax, 0d62f105dh
    and  r9d, r11d
    add  eax, r9d
    mov  r9d, ecx
    and  r9d, r10d
    add  eax, r9d
    rol  eax, 5
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 40]
    not  r9d
    add  ecx, 002441453h
    and  r9d, r10d
    add  ecx, r9d
    mov  r9d, r11d
    and  r9d, eax
    add  ecx, r9d
    rol  ecx, 9
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 60]
    not  r9d
    add  r11d, 0d8a1e681h
    and  r9d, eax
    add  r11d, r9d
    mov  r9d, r10d
    and  r9d, ecx
    add  r11d, r9d
    rol  r11d, 14
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 16]
    not  r9d
    add  r10d, 0e7d3fbc8h
    and  r9d, ecx
    add  r10d, r9d
    mov  r9d, eax
    and  r9d, r11d
    add  r10d, r9d
    rol  r10d, 20
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 36]
    not  r9d
    add  eax, 021e1cde6h
    and  r9d, r11d
    add  eax, r9d
    mov  r9d, ecx
    and  r9d, r10d
    add  eax, r9d
    rol  eax, 5
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 56]
    not  r9d
    add  ecx, 0c33707d6h
    and  r9d, r10d
    add  ecx, r9d
    mov  r9d, r11d
    and  r9d, eax
    add  ecx, r9d
    rol  ecx, 9
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 12]
    not  r9d
    add  r11d, 0f4d50d87h
    and  r9d, eax
    add  r11d, r9d
    mov  r9d, r10d
    and  r9d, ecx
    add  r11d, r9d
    rol  r11d, 14
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 32]
    not  r9d
    add  r10d, 0455a14edh
    and  r9d, ecx
    add  r10d, r9d
    mov  r9d, eax
    and  r9d, r11d
    add  r10d, r9d
    rol  r10d, 20
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 52]
    not  r9d
    add  eax, 0a9e3e905h
    and  r9d, r11d
    add  eax, r9d
    mov  r9d, ecx
    and  r9d, r10d
    add  eax, r9d
    rol  eax, 5
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 8]
    not  r9d
    add  ecx, 0fcefa3f8h
    and  r9d, r10d
    add  ecx, r9d
    mov  r9d, r11d
    and  r9d, eax
    add  ecx, r9d
    rol  ecx, 9
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 28]
    not  r9d
    add  r11d, 0676f02d9h
    and  r9d, eax
    add  r11d, r9d
    mov  r9d, r10d
    and  r9d, ecx
    add  r11d, r9d
    rol  r11d, 14
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 48]
    not  r9d
    add  r10d, 08d2a4c8ah
    and  r9d, ecx
    add  r10d, r9d
    mov  r9d, eax
    and  r9d, r11d
    add  r10d, r9d
    rol  r10d, 20
    add  r10d, r11d
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 20]
    xor  r9d, ecx
    add  eax, 0fffa3942h
    xor  r9d, r10d
    add  eax, r9d
    rol  eax, 4
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 32]
    xor  r9d, r11d
    add  ecx, 08771f681h
    xor  r9d, eax
    add  ecx, r9d
    rol  ecx, 11
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 44]
    xor  r9d, r10d
    add  r11d, 06d9d6122h
    xor  r9d, ecx
    add  r11d, r9d
    rol  r11d, 16
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 56]
    xor  r9d, eax
    add  r10d, 0fde5380ch
    xor  r9d, r11d
    add  r10d, r9d
    rol  r10d, 23
    add  r10d, r11d
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 4]
    xor  r9d, ecx
    add  eax, 0a4beea44h
    xor  r9d, r10d
    add  eax, r9d
    rol  eax, 4
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 16]
    xor  r9d, r11d
    add  ecx, 04bdecfa9h
    xor  r9d, eax
    add  ecx, r9d
    rol  ecx, 11
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 28]
    xor  r9d, r10d
    add  r11d, 0f6bb4b60h
    xor  r9d, ecx
    add  r11d, r9d
    rol  r11d, 16
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 40]
    xor  r9d, eax
    add  r10d, 0bebfbc70h
    xor  r9d, r11d
    add  r10d, r9d
    rol  r10d, 23
    add  r10d, r11d
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 52]
    xor  r9d, ecx
    add  eax, 0289b7ec6h
    xor  r9d, r10d
    add  eax, r9d
    rol  eax, 4
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 0]
    xor  r9d, r11d
    add  ecx, 0eaa127fah
    xor  r9d, eax
    add  ecx, r9d
    rol  ecx, 11
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 12]
    xor  r9d, r10d
    add  r11d, 0d4ef3085h
    xor  r9d, ecx
    add  r11d, r9d
    rol  r11d, 16
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 24]
    xor  r9d, eax
    add  r10d, 004881d05h
    xor  r9d, r11d
    add  r10d, r9d
    rol  r10d, 23
    add  r10d, r11d
    mov  r9d, r11d
    add  eax, DWORD PTR [rdx + 36]
    xor  r9d, ecx
    add  eax, 0d9d4d039h
    xor  r9d, r10d
    add  eax, r9d
    rol  eax, 4
    add  eax, r10d
    mov  r9d, r10d
    add  ecx, DWORD PTR [rdx + 48]
    xor  r9d, r11d
    add  ecx, 0e6db99e5h
    xor  r9d, eax
    add  ecx, r9d
    rol  ecx, 11
    add  ecx, eax
    mov  r9d, eax
    add  r11d, DWORD PTR [rdx + 60]
    xor  r9d, r10d
    add  r11d, 01fa27cf8h
    xor  r9d, ecx
    add  r11d, r9d
    rol  r11d, 16
    add  r11d, ecx
    mov  r9d, ecx
    add  r10d, DWORD PTR [rdx + 8]
    xor  r9d, eax
    add  r10d, 0c4ac5665h
    xor  r9d, r11d
    add  r10d, r9d
    rol  r10d, 23
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 0]
    not  r9d
    add  eax, 0f4292244h
    or   r9d, r10d
    xor  r9d, r11d
    add  eax, r9d
    rol  eax, 6
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 28]
    not  r9d
    add  ecx, 0432aff97h
    or   r9d, eax
    xor  r9d, r10d
    add  ecx, r9d
    rol  ecx, 10
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 56]
    not  r9d
    add  r11d, 0ab9423a7h
    or   r9d, ecx
    xor  r9d, eax
    add  r11d, r9d
    rol  r11d, 15
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 20]
    not  r9d
    add  r10d, 0fc93a039h
    or   r9d, r11d
    xor  r9d, ecx
    add  r10d, r9d
    rol  r10d, 21
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 48]
    not  r9d
    add  eax, 0655b59c3h
    or   r9d, r10d
    xor  r9d, r11d
    add  eax, r9d
    rol  eax, 6
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 12]
    not  r9d
    add  ecx, 08f0ccc92h
    or   r9d, eax
    xor  r9d, r10d
    add  ecx, r9d
    rol  ecx, 10
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 40]
    not  r9d
    add  r11d, 0ffeff47dh
    or   r9d, ecx
    xor  r9d, eax
    add  r11d, r9d
    rol  r11d, 15
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 4]
    not  r9d
    add  r10d, 085845dd1h
    or   r9d, r11d
    xor  r9d, ecx
    add  r10d, r9d
    rol  r10d, 21
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 32]
    not  r9d
    add  eax, 06fa87e4fh
    or   r9d, r10d
    xor  r9d, r11d
    add  eax, r9d
    rol  eax, 6
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 60]
    not  r9d
    add  ecx, 0fe2ce6e0h
    or   r9d, eax
    xor  r9d, r10d
    add  ecx, r9d
    rol  ecx, 10
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 24]
    not  r9d
    add  r11d, 0a3014314h
    or   r9d, ecx
    xor  r9d, eax
    add  r11d, r9d
    rol  r11d, 15
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 52]
    not  r9d
    add  r10d, 04e0811a1h
    or   r9d, r11d
    xor  r9d, ecx
    add  r10d, r9d
    rol  r10d, 21
    add  r10d, r11d
    mov  r9d, ecx
    add  eax, DWORD PTR [rdx + 16]
    not  r9d
    add  eax, 0f7537e82h
    or   r9d, r10d
    xor  r9d, r11d
    add  eax, r9d
    rol  eax, 6
    add  eax, r10d
    mov  r9d, r11d
    add  ecx, DWORD PTR [rdx + 44]
    not  r9d
    add  ecx, 0bd3af235h
    or   r9d, eax
    xor  r9d, r10d
    add  ecx, r9d
    rol  ecx, 10
    add  ecx, eax
    mov  r9d, r10d
    add  r11d, DWORD PTR [rdx + 8]
    not  r9d
    add  r11d, 02ad7d2bbh
    or   r9d, ecx
    xor  r9d, eax
    add  r11d, r9d
    rol  r11d, 15
    add  r11d, ecx
    mov  r9d, eax
    add  r10d, DWORD PTR [rdx + 36]
    not  r9d
    add  r10d, 0eb86d391h
    or   r9d, r11d
    xor  r9d, ecx
    add  r10d, r9d
    mov  r9, QWORD PTR [rsp + 16]
    mov  ebx, r11d
    add  ebx, DWORD PTR [r9 + 4]
    rol  r10d, 21
    add  r10d, ebx
    add  eax, DWORD PTR [r9]
    add  r11d, DWORD PTR [r9 + 8]
    add  ecx, DWORD PTR [r9 + 12]
    mov  DWORD PTR [r9], eax
    mov  DWORD PTR [r9 + 4], r10d
    mov  DWORD PTR [r9 + 8], r11d
    mov  DWORD PTR [r9 + 12], ecx
    add  rdx, 64
    dec  r8
    jnz  block
done:
    pop  rbx
    ret
md5_blocks_x64 ENDP

END
