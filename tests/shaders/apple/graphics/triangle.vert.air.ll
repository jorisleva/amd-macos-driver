source_filename = "-"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.0.0"

%struct.DrawParams = type { <4 x float>, i32, i32, i32, i32 }

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define <{ <4 x float> }> @triangle_vertex(i32 noundef %0, ptr addrspace(2) nocapture noundef readonly align 16 dereferenceable(32) "air-buffer-no-alias" %1) local_unnamed_addr #0 {
  %3 = icmp eq i32 %0, 0
  %4 = icmp eq i32 %0, 1
  %5 = select fast i1 %4, <2 x float> <float 5.000000e-01, float -4.843750e-01>, <2 x float> <float -5.000000e-01, float 5.156250e-01>
  %6 = select fast i1 %3, <2 x float> <float -5.000000e-01, float -4.843750e-01>, <2 x float> %5
  %7 = select fast i1 %4, <2 x float> <float 5.000000e-01, float 5.156250e-01>, <2 x float> <float -5.000000e-01, float -4.843750e-01>
  %8 = select fast i1 %3, <2 x float> <float 5.000000e-01, float -4.843750e-01>, <2 x float> %7
  %9 = getelementptr inbounds %struct.DrawParams, ptr addrspace(2) %1, i64 0, i32 1
  %10 = load i32, ptr addrspace(2) %9, align 16, !tbaa !22, !alias.scope !27
  %11 = icmp eq i32 %10, 0
  %12 = select fast i1 %11, <2 x float> %6, <2 x float> %8
  %13 = shufflevector <2 x float> %12, <2 x float> poison, <4 x i32> <i32 0, i32 1, i32 poison, i32 poison>
  %14 = shufflevector <4 x float> %13, <4 x float> <float poison, float poison, float 0.000000e+00, float 1.000000e+00>, <4 x i32> <i32 0, i32 1, i32 6, i32 7>
  %15 = insertvalue <{ <4 x float> }> undef, <4 x float> %14, 0
  ret <{ <4 x float> }> %15
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "approx-func-fp-math"="true" "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!air.vertex = !{!9}
!air.compile_options = !{!16, !17, !18}
!llvm.ident = !{!19}
!air.version = !{!20}
!air.language_version = !{!21}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 2]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{i32 7, !"air.max_device_buffers", i32 31}
!4 = !{i32 7, !"air.max_constant_buffers", i32 31}
!5 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!6 = !{i32 7, !"air.max_textures", i32 128}
!7 = !{i32 7, !"air.max_read_write_textures", i32 8}
!8 = !{i32 7, !"air.max_samplers", i32 16}
!9 = !{ptr @triangle_vertex, !10, !12}
!10 = !{!11}
!11 = !{!"air.position", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!12 = !{!13, !14}
!13 = !{i32 0, !"air.vertex_id", !"air.arg_type_name", !"uint", !"air.arg_name", !"vertexID"}
!14 = !{i32 1, !"air.buffer", !"air.buffer_size", i32 32, !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2, !"air.struct_type_info", !15, !"air.arg_type_size", i32 32, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"DrawParams", !"air.arg_name", !"draw"}
!15 = !{i32 0, i32 16, i32 0, !"float4", !"color", i32 16, i32 4, i32 0, !"uint", !"shape", i32 20, i32 4, i32 0, !"uint", !"padding0", i32 24, i32 4, i32 0, !"uint", !"padding1", i32 28, i32 4, i32 0, !"uint", !"padding2"}
!16 = !{!"air.compile.denorms_disable"}
!17 = !{!"air.compile.fast_math_enable"}
!18 = !{!"air.compile.framebuffer_fetch_enable"}
!19 = !{!"Apple metal version 32023.864 (metalfe-32023.864)"}
!20 = !{i32 2, i32 8, i32 0}
!21 = !{!"Metal", i32 4, i32 0, i32 0}
!22 = !{!23, !26, i64 16}
!23 = !{!"_ZTS10DrawParams", !24, i64 0, !26, i64 16, !26, i64 20, !26, i64 24, !26, i64 28}
!24 = !{!"omnipotent char", !25, i64 0}
!25 = !{!"Simple C++ TBAA"}
!26 = !{!"int", !24, i64 0}
!27 = !{!28}
!28 = distinct !{!28, !29, !"air-alias-scope-arg(1)"}
!29 = distinct !{!29, !"air-alias-scopes(triangle_vertex)"}
