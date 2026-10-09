import os
import json
import glob

def main():
    root = os.path.abspath('.')
    c_files = glob.glob('kernel/**/*.c', recursive=True) + \
              glob.glob('user/**/*.c', recursive=True) + \
              glob.glob('drivers/**/*.c', recursive=True) + \
              glob.glob('fs/**/*.c', recursive=True)

    entries = []
    for f in c_files:
        f_path = os.path.abspath(f)
        if 'user' in f:
            cmd = f'gcc -m64 -ffreestanding -nostdlib -Iinclude -Iuser/libnyota -Iuser/libgui -c "{f_path}"'
        else:
            cmd = f'gcc -m64 -ffreestanding -nostdlib -mno-red-zone -Iinclude -Iinclude/drivers -Iinclude/elf -Iinclude/fs -Iinclude/ipc -Iinclude/net -Iinclude/security -Iinclude/storage -Iinclude/time -c "{f_path}"'
        entries.append({
            'directory': root,
            'command': cmd,
            'file': f_path
        })

    with open('compile_commands.json', 'w') as out:
        json.dump(entries, out, indent=2)
    print(f'Generated compile_commands.json with {len(entries)} entries')

if __name__ == '__main__':
    main()
