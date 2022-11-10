datatype page = Node(data : int)
datatype tlb_entry = Node(present : bool, readable : bool, writable : bool, ppn : int)

// copies data from device to host
method dma_read(vpn : int, 
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>)

    // There must be a tlb entry and ppn dedicated on the host
    // Our DMA transfer method also requires the vpn to be writeable
    requires vpn in host_tlb
    requires host_tlb[vpn].writable

    // If we are reading from the device, there should be data there
    requires vpn in device_tlb
    requires device_tlb[vpn].present
    requires device_tlb[vpn].ppn in device_mem

    // Original host pages are untouched
    ensures forall vp :: vp in host_tlb ==> vp in new_host_tlb
    ensures forall vp :: vp in host_tlb && vp != vpn ==> host_tlb[vp] == new_host_tlb[vp]
    ensures forall pp :: pp in host_mem ==> pp in new_host_mem
    ensures host_tlb[vpn].ppn == new_host_tlb[vpn].ppn
    ensures forall pp :: pp in host_mem ==> pp == host_tlb[vpn].ppn || host_mem[pp] == new_host_mem[pp]

    // The data that we want ends up on the host at the right virtual address
    ensures vpn in new_host_tlb
    ensures new_host_tlb[vpn].present
    ensures new_host_tlb[vpn].readable
    ensures new_host_tlb[vpn].writable == device_tlb[vpn].writable
    ensures new_host_tlb[vpn].ppn in new_host_mem
    ensures new_host_mem[new_host_tlb[vpn].ppn] == device_mem[device_tlb[vpn].ppn]
{
    new_host_mem := host_mem;
    new_host_tlb := host_tlb;

    var device_entry := device_tlb[vpn];
    new_host_tlb := new_host_tlb[
        vpn := tlb_entry.Node(
            true, 
            true, 
            device_entry.writable, 
            host_tlb[vpn].ppn)];

    new_host_mem := new_host_mem[host_tlb[vpn].ppn := device_mem[device_entry.ppn]];
}

method write_tlb(vpn : int, ppn : int, present : bool, readable : bool, writable : bool, device_tlb : map<int, tlb_entry>)
    returns (new_device_tlb : map<int, tlb_entry>)

    // Old data is unchanged
    ensures forall p :: p in device_tlb ==> p in new_device_tlb
    ensures forall p :: p in device_tlb && p != vpn ==> device_tlb[p] == new_device_tlb[p]

    // actual update takes place
    ensures vpn in new_device_tlb
    ensures new_device_tlb[vpn].present == present
    ensures new_device_tlb[vpn].readable == readable
    ensures new_device_tlb[vpn].writable == writable
    ensures new_device_tlb[vpn].ppn == ppn

{
    new_device_tlb := device_tlb[vpn := tlb_entry.Node(present, readable, writable, ppn)];
}

function vpn_present(vpn : int, tlb : map<int, tlb_entry>) : bool
{
    (vpn in tlb && tlb[vpn].present)
}

function ppn_present_and_readable_somewhere(vpn : int, host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, device_mem : map<int, page>, device_tlb : map<int, tlb_entry>) : bool
{
    (vpn in host_tlb && host_tlb[vpn].present ==>
        host_tlb[vpn].readable && 
        host_tlb[vpn].ppn in host_mem) &&
    (vpn in device_tlb && device_tlb[vpn].present ==>
        device_tlb[vpn].readable && 
        device_tlb[vpn].ppn in device_mem)
}

function ppn_readable(vpn : int, mem : map<int, page>, tlb : map<int, tlb_entry>) : bool
{
    vpn in tlb && tlb[vpn].present &&
        tlb[vpn].readable && tlb[vpn].ppn in mem
}

function data_readable_on_host(data : int, vpn : int, host_mem : map<int, page>, host_tlb : map<int, tlb_entry>) : bool
{
    (vpn in host_tlb) &&
    (host_tlb[vpn].present) &&
    (host_tlb[vpn].ppn in host_mem) &&
    (data == host_mem[host_tlb[vpn].ppn].data)
}

function data_and_permissions_unchanged(vpn : int, host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, device_mem : map<int, page>, device_tlb : map<int, tlb_entry>, data : int, 
            new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_mem : map<int, page>, new_device_tlb : map<int, tlb_entry>) : bool
            requires vpn in new_host_tlb
{
    // The actual data must be unchanged
    (vpn in host_tlb && host_tlb[vpn].present ==> host_tlb[vpn].ppn in host_mem ==> data == host_mem[host_tlb[vpn].ppn].data) &&
    (vpn in device_tlb && device_tlb[vpn].present ==> device_tlb[vpn].ppn in device_mem ==> data == device_mem[device_tlb[vpn].ppn].data) &&

    // The permissions must be unchanged
    (vpn in host_tlb && host_tlb[vpn].present ==> new_host_tlb[vpn].readable == host_tlb[vpn].readable) &&
    (vpn in host_tlb && host_tlb[vpn].present ==> new_host_tlb[vpn].writable == host_tlb[vpn].writable) &&
    (vpn in device_tlb && device_tlb[vpn].present ==> new_host_tlb[vpn].readable == device_tlb[vpn].readable) &&
    (vpn in device_tlb && device_tlb[vpn].present ==> new_host_tlb[vpn].writable == device_tlb[vpn].writable)
}

// read from memory on the host
method read_host(vpn : int, 
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (data : int, 
            new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_mem : map<int, page>, new_device_tlb : map<int, tlb_entry>)

    // data must exist on the host so that we have a ppn for it
    requires vpn in host_tlb
    // Data needs to be present somewhere
    requires vpn_present(vpn, host_tlb) || vpn_present(vpn, device_tlb)
    // Data must not already exist on both the host and device
    requires !(vpn_present(vpn, host_tlb) && vpn_present(vpn, device_tlb))

    // Data must also exist and be readable somewhere
    requires ppn_readable(vpn, host_mem, host_tlb) || ppn_readable(vpn, device_mem, device_tlb)

    // The data should reside on the host after executing this method
    ensures ppn_readable(vpn, new_host_mem, new_host_tlb)

    // The data should not be accessible on the device after executing this method
    ensures !vpn_present(vpn, new_device_tlb)

    ensures data_and_permissions_unchanged(vpn, host_mem, host_tlb, device_mem, device_tlb, data, new_host_mem, new_host_tlb, new_device_mem, new_device_tlb)
{
    new_host_mem := host_mem;
    new_host_tlb := host_tlb;

    new_device_mem := device_mem;
    new_device_tlb := device_tlb;

    if vpn !in host_tlb || !host_tlb[vpn].present {
        new_device_tlb := write_tlb(vpn, 0, false, false, false, device_tlb);
        var curr_entry := host_tlb[vpn];
        var writeable_host_tlb := host_tlb[vpn := tlb_entry.Node(true, true, true, curr_entry.ppn)];
        new_host_mem, new_host_tlb := dma_read(vpn, host_mem, writeable_host_tlb, device_mem, device_tlb);
    }

    data := new_host_mem[new_host_tlb[vpn].ppn].data;
    return;
}

