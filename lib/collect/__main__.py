import argparse,os
import collect

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=str, default="/", help="source directory")
    parser.add_argument("--var", type=str, action="append", default=[], help="variable")
    parser.add_argument("lstfile", type=str, help=".lst file")
    parser.add_argument("destination", type=str, help="destination directory")
    args = parser.parse_args()

    if os.getuid() != 0: raise Exception("You must be a root user.")

    destination = args.destination
    if destination.strip() == "": destination = "."
    if os.path.abspath(destination) in ["/", "//"]: raise Exception("Setting system root directory as destination is totally insane.")

    context = collect.Context(args.source, destination, dict(map(lambda x:collect.parse_var(x), args.var)))
    collect.run(args.lstfile, context)
