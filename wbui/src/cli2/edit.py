import argparse,subprocess,os,tempfile
import cli2.create as create

def read_in_text(device):
    with create.mount_vm(device, True) as tempdir:
        configfile = os.path.join(tempdir, "etc/xen/config")
        return open(configfile).read() if os.path.isfile(configfile) else ""

def read(device):
    vals = {}
    config = read_in_text(device)
    exec config in None, vals
    return vals

def write(device, vals):
    with create.mount_vm(device, False) as tempdir:
        xen_dir = os.path.join(tempdir, "etc/xen")
        if not os.path.isdir(xen_dir): os.makedirs(xen_dir)
        with open(os.path.join(xen_dir, "config"), "w") as f:
            for key, value in vals.iteritems():
                f.write("%s=%s\n" % (key, repr(value)))

def run(name):
    device, name = create.get_device_and_vmname(name)
    config = read_in_text(device)
    editor = os.environ.get("EDITOR") or "vi"
        
    with tempfile.NamedTemporaryFile(delete=True) as tmpfile:
        tmpfile.write(config)
        tmpfile.flush()
        subprocess.call([editor,tmpfile.name])
        tmpfile.seek(0)
        new_config = tmpfile.read()

    vals = {}
    exec new_config in None, vals

    if "memory" not in vals: raise Exception("memory= is mandatory")

    write(device, vals)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("name", type=str, help="VM name or device")
    args = parser.parse_args()
    run(args.name)
