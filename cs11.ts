//%color=#444444 icon="\uf07b"
namespace CS11 {
    let sdFlag=false
    //%block="CS11 size of file %u"
    //%u.defl="log.txt"
    function sizeOfFile(u: string): number {
        if(sdFlag==false) {
            createFolder("CS11")
            sdFlag=true
        }
        return size(u)
    }

    //%block="CS11 remove file"
    //%u.defl="log.txt"
    export function removeFile(u: string): void {
        if(sdFlag==false) {
            createFolder("CS11")
            sdFlag=true
        }
        remove(u)
        return
    }

    //%block="CS11 file %u exists"
    //%u.defl="log.txt"
    export function fileExists(u: string): boolean {
        if(sdFlag==false) {
            createFolder("CS11")
            sdFlag=true
        }
        return exists(u)
    }

    //%block="CS11 overwrite file %u with %v"
    //%u.defl="log.txt"
    export function overwriteFile(u: string, v: string): void {
        if(sdFlag==false) {
            createFolder("CS11")
            sdFlag=true
        }
        file(u, v, 0x02|0x08)
        return
    }

    //%block="CS11 append file %u with %v"
    //%u.defl="log.txt"
    export function appendFile(u: string, v: string): void {
        if(sdFlag==false) {
            createFolder("CS11")
            sdFlag=true
        }
        file(u, v, 0x02|0x30)
        return
    }

    //%block="CS11 append file %u with line %v"
    //%u.defl="log.txt"
    export function appendFileLine(u: string, v: string): void {
        if(sdFlag==false) {
            createFolder("CS11")
            sdFlag=true
        }
        file(u, v + "\n", 0x02|0x30)
        return
    }

    //%block="CS11 read file %u"
    //%u.defl="log.txt"
    export function readFile(u: string): string {
        if(sdFlag==false) {
            createFolder("CS11")
            sdFlag=true
        }
        return file_read(u)
    }

    //%block="CS11 create folder %u"
    function createFolder(u: string): void {
        mkdir(u)
        return;
    }

    //%shim=cs11::_mkdir
    function mkdir(u: string): void {
        return
    }

    //%shim=cs11::_remove
    function remove(u: string): void {
        return
    }

    //%shim=cs11::_file
    function file(u: string, v: string, x: number): boolean {
        return true
    }

    //%shim=cs11::_size
    function size(u: string): number {
        return 1
    }

    //%shim=cs11::_exists
    function exists(u: string): boolean {
        return true
    }

    //%shim=cs11::_read
    function file_read(u: string): string {
        return ""
    }
}