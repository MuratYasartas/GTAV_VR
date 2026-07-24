import os
import sys

def convert_cso_to_header(cso_file, header_file, array_name):
    with open(cso_file, 'rb') as f:
        content = f.read()

    with open(header_file, 'w') as f:
        f.write('#pragma once\n\n')
        f.write('const unsigned char %s[] = {\n' % array_name)
        f.write('    ')
        for i, byte in enumerate(content):
            f.write('0x%02x, ' % byte)
            if (i + 1) % 16 == 0:
                f.write('\n    ')
        f.write('\n};\n')

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: python cso_to_header.py <input.cso> <output.h> <array_name>")
        sys.exit(1)
    
    convert_cso_to_header(sys.argv[1], sys.argv[2], sys.argv[3])
