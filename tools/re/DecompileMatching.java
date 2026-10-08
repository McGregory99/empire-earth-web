// Decompile functions whose qualified name matches a regex.
// Usage (headless): -postScript DecompileMatching.java <output.c> <regex>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.regex.Pattern;

public class DecompileMatching extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Pattern p = Pattern.compile(args[1]);
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
            int n = 0;
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                String q = f.getName(true);
                if (f.isThunk() || f.isExternal() || !p.matcher(q).find()) continue;
                DecompileResults r = ifc.decompileFunction(f, 120, monitor);
                w.printf("/* %s @ %s size=%d */%n", q, f.getEntryPoint(), f.getBody().getNumAddresses());
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "/* decompile failed: " + r.getErrorMessage() + " */");
                n++;
            }
            println("decompiled " + n + " matching functions -> " + args[0]);
        } finally {
            ifc.dispose();
        }
    }
}
