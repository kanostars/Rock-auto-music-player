using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.IO.Compression;
using System.Text.Json;

var source = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
Directory.CreateDirectory(output);
var data = File.ReadAllBytes(source);
var signature = Convert.FromHexString("8b1202b96a612038727b930214d7a03213f5b9e6efae3318ee3b2dce24b36aae");
var marker = data.AsSpan().IndexOf(signature);
if (marker < 8) throw new InvalidDataException("No .NET bundle signature");
using var reader = new BinaryReader(new MemoryStream(data));
reader.BaseStream.Position = BitConverter.ToInt64(data, marker - 8);
var major = reader.ReadUInt32(); var minor = reader.ReadUInt32();
var count = reader.ReadInt32(); var bundleId = reader.ReadString();
if (major >= 2) reader.BaseStream.Position += 40;
var entries = new List<object>();
var audio = new List<object>();
var opcodes = typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)
    .Where(f => f.FieldType == typeof(OpCode)).Select(f => (OpCode)f.GetValue(null)!)
    .ToDictionary(o => unchecked((ushort)o.Value));
using var listing = new StreamWriter(Path.Combine(output, "audio-code-il.txt"));
for (int i = 0; i < count; i++) {
    long offset = reader.ReadInt64(), size = reader.ReadInt64();
    long compressed = major >= 6 ? reader.ReadInt64() : 0;
    byte kind = reader.ReadByte(); string name = reader.ReadString();
    if (offset < 0 || size < 0 || offset + (compressed > 0 ? compressed : size) > data.LongLength)
        throw new InvalidDataException(name);
    entries.Add(new { name, offset, size, compressed, kind });
    if (kind != 1 || !(name.StartsWith("Sift", StringComparison.OrdinalIgnoreCase) || name.StartsWith("Workbench", StringComparison.OrdinalIgnoreCase))) continue;
    byte[] assembly = data.AsSpan((int)offset, (int)(compressed > 0 ? compressed : size)).ToArray();
    if (compressed > 0) {
        using var inflate = new DeflateStream(new MemoryStream(assembly), CompressionMode.Decompress);
        using var buffer = new MemoryStream(); inflate.CopyTo(buffer); assembly = buffer.ToArray();
    }
    if (assembly.LongLength != size) throw new InvalidDataException("Assembly size mismatch");
    using var pe = new PEReader(new MemoryStream(assembly));
    var md = pe.GetMetadataReader();
    string TokenName(int token) {
        try {
            var h = MetadataTokens.Handle(token);
            return h.Kind switch {
                HandleKind.UserString => JsonSerializer.Serialize(md.GetUserString((UserStringHandle)h)),
                HandleKind.MethodDefinition => md.GetString(md.GetMethodDefinition((MethodDefinitionHandle)h).Name),
                HandleKind.MemberReference => md.GetString(md.GetMemberReference((MemberReferenceHandle)h).Name),
                HandleKind.FieldDefinition => md.GetString(md.GetFieldDefinition((FieldDefinitionHandle)h).Name),
                HandleKind.TypeDefinition => md.GetString(md.GetTypeDefinition((TypeDefinitionHandle)h).Name),
                HandleKind.TypeReference => md.GetString(md.GetTypeReference((TypeReferenceHandle)h).Name),
                _ => h.Kind.ToString()
            };
        } catch { return "?"; }
    }
    foreach (var h in md.ManifestResources) {
        var r = md.GetManifestResource(h); var resourceName = md.GetString(r.Name);
        if (!r.Implementation.IsNil || !resourceName.EndsWith(".wav", StringComparison.OrdinalIgnoreCase)) continue;
        var block = pe.GetSectionData(pe.PEHeaders.CorHeader!.ResourcesDirectory.RelativeVirtualAddress);
        var blob = block.GetReader((int)r.Offset, block.Length - (int)r.Offset);
        int length = blob.ReadInt32(); var bytes = blob.ReadBytes(length);
        if (bytes.Length < 12 || System.Text.Encoding.ASCII.GetString(bytes,0,4) != "RIFF" || System.Text.Encoding.ASCII.GetString(bytes,8,4) != "WAVE")
            throw new InvalidDataException(resourceName);
        var shortName = resourceName.Split("HandpanSamples.").Last();
        if (Path.GetFileName(shortName) != shortName) throw new InvalidDataException(shortName);
        Directory.CreateDirectory(Path.Combine(output, "all_samples"));
        File.WriteAllBytes(Path.Combine(output, "all_samples", shortName), bytes);
        audio.Add(new { assembly = name, resourceName, file = "all_samples/" + shortName, length,
            sha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)).ToLowerInvariant() });
        Console.WriteLine($"{shortName}: {length} bytes");
    }
    foreach (var th in md.TypeDefinitions) {
        var type = md.GetTypeDefinition(th); string typeName = md.GetString(type.Name);
        if (!(typeName.Contains("Audio", StringComparison.OrdinalIgnoreCase) || typeName.Contains("Sample", StringComparison.OrdinalIgnoreCase) || typeName.Contains("InputTest", StringComparison.OrdinalIgnoreCase) || typeName.Contains("Handpan", StringComparison.OrdinalIgnoreCase))) continue;
        foreach (var mh in type.GetMethods()) {
            var method = md.GetMethodDefinition(mh);
            if (method.RelativeVirtualAddress == 0) continue;
            listing.WriteLine($"\n{md.GetString(type.Namespace)}.{typeName}::{md.GetString(method.Name)}");
            var il = pe.GetMethodBody(method.RelativeVirtualAddress).GetILBytes();
            for (int p = 0; p < il.Length;) {
                int start = p; ushort code = il[p++]; if (code == 0xfe) code = (ushort)(0xfe00 | il[p++]);
                var op = opcodes[code]; string operand = "";
                int n = op.OperandType switch {
                    OperandType.InlineNone => 0, OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
                    OperandType.InlineVar => 2, OperandType.InlineI8 or OperandType.InlineR => 8,
                    OperandType.InlineSwitch => 4 + 4 * BitConverter.ToInt32(il,p), _ => 4
                };
                if (n == 1) operand = ((sbyte)il[p]).ToString();
                else if (n == 2) operand = BitConverter.ToUInt16(il,p).ToString();
                else if (n == 4) {
                    int value = BitConverter.ToInt32(il,p); operand = value.ToString();
                    if (op.OperandType is OperandType.InlineString or OperandType.InlineMethod or OperandType.InlineField or OperandType.InlineType or OperandType.InlineTok)
                        operand = $"0x{value:x8} {TokenName(value)}";
                    else if (op.OperandType == OperandType.ShortInlineR) operand = BitConverter.ToSingle(il,p).ToString();
                } else if (n == 8) operand = op.OperandType == OperandType.InlineR ? BitConverter.ToDouble(il,p).ToString() : BitConverter.ToInt64(il,p).ToString();
                listing.WriteLine($"  {start:x4}: {op.Name} {operand}"); p += n;
            }
        }
    }
}
var options = new JsonSerializerOptions { WriteIndented = true };
File.WriteAllText(Path.Combine(output,"bundle-manifest.json"), JsonSerializer.Serialize(new { source, major, minor, bundleId, entries }, options));
File.WriteAllText(Path.Combine(output,"resource-manifest.json"), JsonSerializer.Serialize(audio, options));
Console.WriteLine($"Extracted {audio.Count} WAV resources without executing the source program.");
