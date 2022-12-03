int main()
{
    Graph g = read_graph();
    prepare(g);
    copy_to_fpga(g);
    run_fpga(g);
    copy_to_host(g);
    print(g);
}

int main()
{
    Graph g = fsrf_mmap(graph.txt);

    run_fpga(g);
    fsrf_msync(g);
    print(g);
}

int main()
{
    Graph g = read_graph();

    run_fpga(g);

    print(g);
}
