datatype page = Node(data : int)
datatype tlb_entry = Node(present : bool, readable : bool, writable : bool, ppn : int)

function no_page_aliasing(tlb : map<int, tlb_entry>) : bool 
{
    forall vpn1, vpn2 :: vpn1 != vpn2 && 
        vpn1 in tlb &&
        vpn2 in tlb 
        ==> tlb[vpn1].ppn != tlb[vpn2].ppn
}

// copies data from device to host
method dma_read(vpn : int, 
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (new_host_mem : map<int, page>)

    // There must be a tlb entry and ppn dedicated on the host
    // Our DMA transfer method also requires the vpn to be writeable
    requires vpn in host_tlb
    requires legal_vaddr(vpn, host_mem, host_tlb)
    requires no_page_aliasing(host_tlb) && no_page_aliasing(device_tlb)

    // We are reading from the device, there should be data there
    requires(vpn_present(vpn, device_mem, device_tlb))

    // Original host/device pages are untouched
    ensures no_side_effects(vpn, host_mem, host_tlb, new_host_mem, host_tlb)
    ensures valid_ppns(host_mem, host_tlb) ==> valid_ppns(new_host_mem, host_tlb)
    ensures valid_memory(host_mem, host_tlb, device_mem, device_tlb) && vpn !in host_tlb ==> 
            valid_memory(new_host_mem, host_tlb, device_mem, device_tlb)
    ensures valid_memory(host_mem, host_tlb, device_mem, device_tlb) && 
            !ppn_writable(vpn, device_mem, device_tlb) && 
            host_tlb[vpn].ppn in new_host_mem && 
            new_host_mem[host_tlb[vpn].ppn] == device_mem[device_tlb[vpn].ppn] ==> 
            valid_memory(new_host_mem, host_tlb, device_mem, device_tlb)
    // ensures valid_memory(new_host_mem, host_tlb, device_mem, device_tlb)

    // The data is unchanged
    ensures host_tlb[vpn].ppn in new_host_mem
    // ensures device_tlb[vpn].ppn in device_mem
    ensures new_host_mem[host_tlb[vpn].ppn] == device_mem[device_tlb[vpn].ppn]
{
    new_host_mem := host_mem;
    var device_entry := device_tlb[vpn];

    new_host_mem := new_host_mem[host_tlb[vpn].ppn := device_mem[device_entry.ppn]];
}


// copies data from host to device
method dma_write(vpn : int, 
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (new_device_mem : map<int, page>)

    // There must be a tlb entry and ppn dedicated on the host
    // Our DMA transfer method also requires the vpn to be writeable
    requires vpn in host_tlb
    requires legal_vaddr(vpn, host_mem, host_tlb)

    // We are reading from the device, there should be data there
    requires(vpn_present(vpn, device_mem, device_tlb))
    requires no_page_aliasing(host_tlb) && no_page_aliasing(device_tlb)


    // Original host/device pages are untouched
    ensures no_side_effects(vpn, device_mem, device_tlb, new_device_mem, device_tlb)
    ensures valid_ppns(device_mem, device_tlb) ==> valid_ppns(new_device_mem, device_tlb)
    ensures valid_memory(host_mem, host_tlb, device_mem, device_tlb) ==> valid_memory(host_mem, host_tlb, new_device_mem, device_tlb)

    // The data is unchanged
    ensures device_tlb[vpn].ppn in new_device_mem
    ensures new_device_mem[device_tlb[vpn].ppn] == host_mem[host_tlb[vpn].ppn]
{
    new_device_mem := device_mem;
    var host_entry := host_tlb[vpn];

    new_device_mem := new_device_mem[device_tlb[vpn].ppn := host_mem[host_entry.ppn]];
}

method write_tlb(vpn : int, ppn : int, present : bool, readable : bool, writable : bool, tlb : map<int, tlb_entry>, ghost mem : map<int, page>, ghost other_tlb : map<int, tlb_entry>, ghost other_mem : map<int, page>)
    returns (new_tlb : map<int, tlb_entry>)

    // Old data is unchanged
    requires no_page_aliasing(tlb)

    ensures no_side_effects(vpn, mem, tlb, mem, new_tlb)
    ensures valid_ppns(mem, tlb) && !present ==> valid_ppns(mem, new_tlb)
    ensures valid_ppns(mem, tlb) && present && ppn in mem ==> valid_ppns(mem, new_tlb)
    ensures valid_memory(mem, tlb, other_mem, other_tlb) && !present ==> valid_memory(mem, new_tlb, other_mem, other_tlb)
    ensures valid_memory(mem, tlb, other_mem, other_tlb) && 
        ppn in mem && !vpn_present(vpn, other_mem, other_tlb) ==> valid_memory(mem, new_tlb, other_mem, other_tlb)
    ensures valid_memory(mem, tlb, other_mem, other_tlb) && 
        ppn in mem && 
        readable && !writable &&
        ppn_readable(vpn, other_mem, other_tlb) && !ppn_writable(vpn, other_mem, other_tlb) &&
        mem[ppn] == other_mem[other_tlb[vpn].ppn] ==> valid_memory(mem, new_tlb, other_mem, other_tlb)
    ensures valid_memory(mem, tlb, other_mem, other_tlb) && 
        ppn in mem && 
        readable && !writable &&
        !ppn_readable(vpn, other_mem, other_tlb) && !ppn_writable(vpn, other_mem, other_tlb)
        ==> valid_memory(mem, new_tlb, other_mem, other_tlb)
    // ensures valid_memory(mem, tlb, other_mem, other_tlb) && ppn in mem && !writable && !ppn_writable(vpn, other_mem, other_tlb) ==> valid_memory(mem, new_tlb, other_mem, other_tlb)
    
    ensures (vpn in tlb && ppn == tlb[vpn].ppn) ==> no_page_aliasing(new_tlb)
    ensures (forall vpn :: vpn in tlb ==> tlb[vpn].ppn != ppn) ==> no_page_aliasing(new_tlb)

    // actual update takes place
    ensures vpn in new_tlb
    ensures new_tlb[vpn].present == present
    ensures new_tlb[vpn].readable == readable
    ensures new_tlb[vpn].writable == writable
    ensures new_tlb[vpn].ppn == ppn
{
    new_tlb := tlb[vpn := tlb_entry.Node(present, readable, writable, ppn)];
}
function legal_vaddr(vpn : int, mem : map<int, page>, tlb : map<int, tlb_entry>) : bool
{
    (vpn in tlb) && (tlb[vpn].ppn in mem)
}

function vpn_present(vpn : int, mem : map<int, page>, tlb : map<int, tlb_entry>) : bool
{
    (legal_vaddr(vpn, mem, tlb) && tlb[vpn].present)
}

function ppn_readable(vpn : int, mem : map<int, page>, tlb : map<int, tlb_entry>) : bool
{
    vpn in tlb && tlb[vpn].present &&
        tlb[vpn].readable && tlb[vpn].ppn in mem
}

function ppn_writable(vpn : int, mem : map<int, page>, tlb : map<int, tlb_entry>) : bool
{
    vpn in tlb && tlb[vpn].present &&
        tlb[vpn].writable && tlb[vpn].ppn in mem
}

function data_readable_on_host(data : int, vpn : int, host_mem : map<int, page>, host_tlb : map<int, tlb_entry>) : bool
{
    (vpn in host_tlb) &&
    (host_tlb[vpn].present) &&
    (host_tlb[vpn].ppn in host_mem) &&
    (data == host_mem[host_tlb[vpn].ppn].data)
}

function no_side_effects(vpn : int, mem : map<int, page>, tlb : map<int, tlb_entry>, new_mem : map<int, page>, new_tlb : map<int, tlb_entry>) : bool {
    var tlb_unchanged := (forall v :: v in tlb && v != vpn ==> 
        v in new_tlb && tlb[v] == new_tlb[v]);

    var mem_unchanged := if vpn in tlb then
        forall pp :: pp in mem && tlb[vpn].ppn != pp ==> pp in new_mem && mem[pp] == new_mem[pp]
    else
        forall pp :: pp in mem ==> pp in new_mem && mem[pp] == new_mem[pp];
    
    tlb_unchanged && mem_unchanged
} 

function data_unchanged(vpn : int, host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, device_mem : map<int, page>, device_tlb : map<int, tlb_entry>, data : int, 
            new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_mem : map<int, page>, new_device_tlb : map<int, tlb_entry>) : bool
            requires ppn_readable(vpn, new_host_mem, new_host_tlb) || ppn_readable(vpn, new_device_mem, new_device_tlb)
            requires ppn_readable(vpn, host_mem, host_tlb) || ppn_readable(vpn, device_mem, device_tlb)
{
    var saved_data := if ppn_readable(vpn, new_host_mem, new_host_tlb) then 
        new_host_mem[new_host_tlb[vpn].ppn].data
    else 
        new_device_mem[new_device_tlb[vpn].ppn].data;

    var original_data := if ppn_readable(vpn, host_mem, host_tlb) then
        host_mem[host_tlb[vpn].ppn].data
    else
        device_mem[device_tlb[vpn].ppn].data;
    
    saved_data == data && saved_data == original_data
}

function permissions_unchanged(vpn : int, host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, device_mem : map<int, page>, device_tlb : map<int, tlb_entry>, 
            new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_mem : map<int, page>, new_device_tlb : map<int, tlb_entry>) : bool
            // requires vpn_present(vpn, new_host_mem, new_host_tlb) || vpn_present(vpn, new_device_mem, new_device_tlb)
            // requires vpn_present(vpn, host_mem, host_tlb) || vpn_present(vpn, device_mem, device_tlb)
{
    // The permissions must be unchanged
    (vpn_present(vpn, host_mem, host_tlb) && vpn_present(vpn, new_host_mem, new_host_tlb) ==> new_host_tlb[vpn] == host_tlb[vpn]) &&
    (vpn_present(vpn, device_mem, device_tlb) && vpn_present(vpn, new_host_mem, new_host_tlb) ==> (new_host_tlb[vpn].readable == device_tlb[vpn].readable) && new_host_tlb[vpn].writable == device_tlb[vpn].writable) &&

    (vpn_present(vpn, device_mem, device_tlb) && vpn_present(vpn, new_device_mem, new_device_tlb) ==> new_device_tlb[vpn] == device_tlb[vpn]) &&
    (vpn_present(vpn, host_mem, host_tlb) && vpn_present(vpn, new_device_mem, new_device_tlb) ==> (host_tlb[vpn].readable == new_device_tlb[vpn].readable) && host_tlb[vpn].writable == new_device_tlb[vpn].writable)
}

function valid_ppns(mem : map<int, page>, tlb : map<int, tlb_entry>) : bool 

{
    (forall vpage :: vpage in tlb && tlb[vpage].present ==> tlb[vpage].ppn in mem)
}

function valid_memory(mem1 : map<int, page>, tlb1 : map<int, tlb_entry>, mem2 : map<int, page>, tlb2 : map<int, tlb_entry>) : bool 
{
    var ppns_valid := valid_ppns(mem1, tlb1) && valid_ppns(mem2, tlb2);
    var no_duplicate_writable_pages := 
        forall vpn ::
            !(ppn_writable(vpn, mem1, tlb1) && vpn_present(vpn, mem2, tlb2)) &&
            !(ppn_writable(vpn, mem2, tlb2) && vpn_present(vpn, mem1, tlb1));

    var readonly_pages_equal :=
        forall vpn ::
            ppn_readable(vpn, mem1, tlb1) && ppn_readable(vpn, mem2, tlb2) ==> mem1[tlb1[vpn].ppn] == mem2[tlb2[vpn].ppn];

    ppns_valid && no_duplicate_writable_pages && readonly_pages_equal
}

// read from memory on the host
method read_host(vpn : int, 
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (data : int, 
            new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_tlb : map<int, tlb_entry>)

    // data must exist on the host so that we have a ppn for it
    requires legal_vaddr(vpn, host_mem, host_tlb)
    // Data must also exist and be readable somewhere
    requires ppn_readable(vpn, host_mem, host_tlb) || ppn_readable(vpn, device_mem, device_tlb)
    requires valid_memory(host_mem, host_tlb, device_mem, device_tlb)
    requires no_page_aliasing(host_tlb) && no_page_aliasing(device_tlb)

    // ensures vpn_present(vpn, new_host_mem, new_host_tlb)
    // The data should reside on the host after executing this method
    ensures ppn_readable(vpn, new_host_mem, new_host_tlb)

    // The data should not be accessible on the device after executing this method
    ensures !ppn_writable(vpn, device_mem, new_device_tlb)

    ensures data_unchanged(vpn, host_mem, host_tlb, device_mem, device_tlb, data, new_host_mem, new_host_tlb, device_mem, new_device_tlb)

    // pages other than vpn are not changed in any way
    ensures no_side_effects(vpn, host_mem, host_tlb, new_host_mem, new_host_tlb)
    ensures no_side_effects(vpn, device_mem, device_tlb, device_mem, new_device_tlb)

    // ensures permissions_unchanged(vpn, host_mem, host_tlb, device_mem, device_tlb, new_host_mem, new_host_tlb, device_mem, new_device_tlb)
    ensures valid_memory(new_host_mem, new_host_tlb, device_mem, new_device_tlb)
    ensures no_page_aliasing(new_host_tlb) && no_page_aliasing(new_device_tlb)

{
    new_host_mem := host_mem;
    new_host_tlb := host_tlb;

    new_device_tlb := device_tlb;

    if vpn !in host_tlb || !host_tlb[vpn].present || !host_tlb[vpn].readable {
        var old_entry := device_tlb[vpn];
        new_device_tlb := write_tlb(vpn, old_entry.ppn, true, true, false, device_tlb, device_mem, host_tlb, host_mem);
        var curr_entry := host_tlb[vpn];
        new_host_mem := dma_read(vpn, host_mem, host_tlb, device_mem, device_tlb);
        new_host_tlb := write_tlb(vpn, curr_entry.ppn, true, true, false, new_host_tlb, new_host_mem, new_device_tlb, device_mem);
    }
    data := new_host_mem[new_host_tlb[vpn].ppn].data;
    return;
}

method write_host(vpn : int, data : int,
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_mem : map<int, page>, new_device_tlb : map<int, tlb_entry>)
    
       // data must exist on the host so that we have a ppn for it
    requires legal_vaddr(vpn, host_mem, host_tlb)
    // Data must also exist and be readable somewhere
    requires ppn_writable(vpn, host_mem, host_tlb) || ppn_writable(vpn, device_mem, device_tlb)
    requires valid_memory(host_mem, host_tlb, device_mem, device_tlb)
    requires no_page_aliasing(host_tlb) && no_page_aliasing(device_tlb)

    // The data should reside on the host after executing this method
    ensures ppn_writable(vpn, new_host_mem, new_host_tlb)

    // read permissions are unchanged
    ensures (ppn_readable(vpn, host_mem, host_tlb) || ppn_readable(vpn, device_mem, device_tlb)) 
        == ppn_readable(vpn, new_host_mem, new_host_tlb)
    ensures new_host_mem[new_host_tlb[vpn].ppn].data == data

    // The data should not be accessible on the device after executing this method
    ensures !vpn_present(vpn, device_mem, new_device_tlb)

    // pages other than vpn are not changed in any way
    ensures no_side_effects(vpn, host_mem, host_tlb, new_host_mem, new_host_tlb)
    ensures no_side_effects(vpn, device_mem, device_tlb, device_mem, new_device_tlb)

    ensures permissions_unchanged(vpn, host_mem, host_tlb, device_mem, device_tlb, new_host_mem, new_host_tlb, device_mem, new_device_tlb)
    ensures valid_memory(new_host_mem, new_host_tlb, device_mem, new_device_tlb)
    ensures no_page_aliasing(new_host_tlb) && no_page_aliasing(new_device_tlb)
{
    new_host_mem := host_mem;
    new_host_tlb := host_tlb;

    new_device_mem := device_mem;
    new_device_tlb := device_tlb;

    if vpn !in host_tlb || !host_tlb[vpn].present {
        var should_be_readable := device_tlb[vpn].readable;
        new_device_tlb := write_tlb(vpn, device_tlb[vpn].ppn, false, false, false, device_tlb, device_mem, host_tlb, host_mem);
        var curr_entry := host_tlb[vpn];
        var writeable_host_tlb := host_tlb[vpn := tlb_entry.Node(true, true, true, curr_entry.ppn)];
        new_host_mem := dma_read(vpn, host_mem, writeable_host_tlb, device_mem, device_tlb);
        new_host_tlb := new_host_tlb[vpn := tlb_entry.Node(true, should_be_readable, true, curr_entry.ppn)];
    }
    new_host_mem := new_host_mem[new_host_tlb[vpn].ppn := page.Node(data)];
    return; 
}

method allocate_device_ppn(device_mem : map<int, page>, ghost device_tlb : map<int, tlb_entry>)
    returns (pg : int, new_device_mem : map<int, page>)
    requires forall ppn :: ppn !in device_mem ==> forall vpn :: vpn in device_tlb ==> device_tlb[vpn].ppn != ppn

    requires no_page_aliasing(device_tlb)

    ensures pg !in device_mem
    ensures pg in new_device_mem
    ensures forall p :: p in device_mem ==> p in new_device_mem && device_mem[p] == new_device_mem[p]
    // forall vpn :: vpn in tlb && tlb[vpn].ppn != ppn
    ensures forall vpn :: vpn in device_tlb ==> device_tlb[vpn].ppn != pg
{
    var keys := device_mem.Keys;
    pg := 0;
    var seen := {};
    while keys != {} 
        invariant |seen| > 0 ==> forall k :: k in seen ==> pg > k
        invariant forall k :: k in device_mem.Keys ==> k in seen || k in keys
    {
        var key :| key in keys;
        keys := keys - {key};
        seen := seen + {key};

        if key >= pg {
            pg := key + 1;
        }
    }

    new_device_mem := device_mem[pg := page.Node(0)];
}

// read from memory on the device
method read_device(vpn : int, 
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (data : int, 
            new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_mem : map<int, page>, new_device_tlb : map<int, tlb_entry>)

    // The tlb only currently points to pages in memory. this is required to guarantee that the allocator 
    // doesn't cause any aliasing
    requires forall ppn :: ppn !in device_mem ==> forall vpn :: vpn in device_tlb ==> device_tlb[vpn].ppn != ppn

    // data must exist on the host so that we have a ppn for it
    requires legal_vaddr(vpn, host_mem, host_tlb)
    // Data must also exist and be readable somewhere
    requires ppn_readable(vpn, host_mem, host_tlb) || ppn_readable(vpn, device_mem, device_tlb)
    requires valid_memory(host_mem, host_tlb, device_mem, device_tlb)
    requires no_page_aliasing(host_tlb) && no_page_aliasing(device_tlb)

    // ensures vpn_present(vpn, new_host_mem, new_host_tlb)
    // The data should reside on the device after executing this method
    ensures ppn_readable(vpn, new_device_mem, new_device_tlb)

    // The data should not be accessible on the host after executing this method
    ensures !ppn_writable(vpn, new_host_mem, new_host_tlb)

    ensures data_unchanged(vpn, host_mem, host_tlb, device_mem, device_tlb, data, new_host_mem, new_host_tlb, new_device_mem, new_device_tlb)

    // pages other than vpn are not changed in any way
    ensures no_side_effects(vpn, host_mem, host_tlb, new_host_mem, new_host_tlb)
    ensures no_side_effects(vpn, device_mem, device_tlb, device_mem, new_device_tlb)

    ensures valid_memory(new_host_mem, new_host_tlb, new_device_mem, new_device_tlb)
    ensures no_page_aliasing(new_host_tlb) && no_page_aliasing(new_device_tlb)
{
    new_host_mem := host_mem;
    new_host_tlb := host_tlb;

    new_device_mem := device_mem;
    new_device_tlb := device_tlb;

    if vpn !in device_tlb || !device_tlb[vpn].present || !device_tlb[vpn].readable {
        var should_be_writable := host_tlb[vpn].writable;
        var ppn;
        ppn, new_device_mem := allocate_device_ppn(device_mem, device_tlb);

        new_host_tlb := write_tlb(vpn, host_tlb[vpn].ppn, false, false, false, host_tlb, new_host_mem, new_device_tlb, new_device_mem);
        new_device_tlb := write_tlb(vpn, ppn, true, true, should_be_writable, new_device_tlb, new_device_mem, new_host_tlb, new_host_mem);
        new_device_mem := dma_write(vpn, host_mem, host_tlb, new_device_mem, new_device_tlb);
    }
    data := new_device_mem[new_device_tlb[vpn].ppn].data;
    return;
}


// read from memory on the device
method write_device(vpn : int, data : int,
                host_mem : map<int, page>, host_tlb : map<int, tlb_entry>, 
                device_mem : map<int, page>, device_tlb : map<int, tlb_entry>)

    returns (new_host_mem : map<int, page>, new_host_tlb : map<int, tlb_entry>, 
            new_device_mem : map<int, page>, new_device_tlb : map<int, tlb_entry>)

    // data must exist on the host so that we have a ppn for it
    requires legal_vaddr(vpn, host_mem, host_tlb)
    // Data must also exist and be readable somewhere
    requires ppn_writable(vpn, host_mem, host_tlb) || ppn_writable(vpn, device_mem, device_tlb)
    requires valid_memory(host_mem, host_tlb, device_mem, device_tlb)

    // ensures vpn_present(vpn, new_host_mem, new_host_tlb)
    // The data should reside on the device after executing this method
    ensures ppn_writable(vpn, new_device_mem, new_device_tlb)

    // read permissions are unchanged
    ensures (ppn_readable(vpn, host_mem, host_tlb) || ppn_readable(vpn, device_mem, device_tlb)) 
        == ppn_readable(vpn, new_device_mem, new_device_tlb)
    ensures new_device_mem[new_device_tlb[vpn].ppn].data == data

    // The data should not be accessible on the host after executing this method
    ensures !vpn_present(vpn, new_host_mem, new_host_tlb)

    // pages other than vpn are not changed in any way
    ensures no_side_effects(vpn, host_mem, host_tlb, new_host_mem, new_host_tlb)
    ensures no_side_effects(vpn, device_mem, device_tlb, device_mem, new_device_tlb)

    ensures permissions_unchanged(vpn, host_mem, host_tlb, device_mem, device_tlb, new_host_mem, new_host_tlb, device_mem, new_device_tlb)
    ensures valid_memory(new_host_mem, new_host_tlb, new_device_mem, new_device_tlb)
{
    new_host_mem := host_mem;
    new_host_tlb := host_tlb;

    new_device_mem := device_mem;
    new_device_tlb := device_tlb;

    if vpn !in device_tlb || !device_tlb[vpn].present {
        var should_be_readable := host_tlb[vpn].readable;
        var ppn;
        ppn, new_device_mem := allocate_device_ppn(device_mem, device_tlb);
        new_host_tlb := write_tlb(vpn, 0, false, false, false, host_tlb, new_host_mem, new_device_tlb, new_device_mem);
        new_device_tlb := write_tlb(vpn, ppn, true, should_be_readable, true, new_device_tlb, new_device_mem, new_host_tlb, new_host_mem);
        new_device_mem := dma_write(vpn, host_mem, host_tlb, new_device_mem, new_device_tlb);
    } 
    // data := new_zdevice_mem[new_device_tlb[vpn].ppn].data;
    new_device_mem := new_device_mem[new_device_tlb[vpn].ppn := page.Node(data)];
    return;
}