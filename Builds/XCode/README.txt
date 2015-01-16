Place XCode project file here!

1) To build in you XCode, you need to: 

mkdir -p build/proto; 
protoc -Isrc/ripple/proto --cpp_out=build/proto src/ripple/proto/ripple.proto

2) To run you vpal by default configuration, you need to copy your vpal.cfg to:
~/.config/ripple/

3) To run vapl by default, you need to mkdir:
mkdir -p ~/vpal/db
