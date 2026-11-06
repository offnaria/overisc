// SPDX-License-Identifier: CERN-OHL-P-2.0
// Pipeline interface of Overisc core.
//
//      /-------------------------\      /-------------------------\
// ---> [sink]    module   [source] ---> [sink]    module   [source] --->
//      \-------------------------/      \-------------------------/

interface overisc_pipe_if #(
    parameter DATA_W = 32
);

  logic [DATA_W-1:0] data;
  logic valid;
  logic ready;

  modport sink(input data, input valid, output ready);
  modport source(output data, output valid, input ready);

  function automatic logic hs();  // handshake success
    return valid && ready;
  endfunction

endinterface
