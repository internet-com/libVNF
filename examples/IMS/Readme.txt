This is the kernel version of IMS ported to library.
Refer to https://github.com/networkedsystemsIITB/NFV_IMS for IMS details. 
Install RAN as given in the above link.
Do the library setup as listed in the user_manual.pdf.

Intall dependencies:
sudo apt-get install libssl-dev 
sudo apt-get install libboost-all-dev

Setup:
Setup each component on separate VMs. Change the number of cores = VM cores in lib.h.
make clean
make
run ./component_name on each VM
