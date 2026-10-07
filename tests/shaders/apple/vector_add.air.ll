source_filename = "-"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.0.0"

%struct.Params = type { i32, i32 }

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite)
define void @vector_add(ptr addrspace(1) nocapture noundef readonly "air-buffer-no-alias" %0, ptr addrspace(1) nocapture noundef readonly "air-buffer-no-alias" %1, ptr addrspace(1) nocapture noundef writeonly "air-buffer-no-alias" %2, ptr addrspace(1) nocapture noundef readonly "air-buffer-no-alias" %3, i32 noundef %4) local_unnamed_addr #0 {
  %6 = getelementptr inbounds %struct.Params, ptr addrspace(1) %3, i64 0, i32 0
  %7 = load i32, ptr addrspace(1) %6, align 4, !tbaa !24, !alias.scope !29, !noalias !32
  %8 = icmp ugt i32 %7, %4
  br i1 %8, label %9, label %20

9:                                                ; preds = %5
  %10 = getelementptr inbounds %struct.Params, ptr addrspace(1) %3, i64 0, i32 1
  %11 = load i32, ptr addrspace(1) %10, align 4, !tbaa !36, !alias.scope !29, !noalias !32
  %12 = add i32 %11, %4
  %13 = zext i32 %12 to i64
  %14 = getelementptr inbounds i32, ptr addrspace(1) %0, i64 %13
  %15 = load i32, ptr addrspace(1) %14, align 4, !tbaa !37, !alias.scope !38, !noalias !39
  %16 = getelementptr inbounds i32, ptr addrspace(1) %1, i64 %13
  %17 = load i32, ptr addrspace(1) %16, align 4, !tbaa !37, !alias.scope !40, !noalias !41
  %18 = add i32 %17, %15
  %19 = getelementptr inbounds i32, ptr addrspace(1) %2, i64 %13
  store i32 %18, ptr addrspace(1) %19, align 4, !tbaa !37, !alias.scope !42, !noalias !43
  br label %20

20:                                               ; preds = %9, %5
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) "approx-func-fp-math"="true" "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!air.kernel = !{!9}
!air.compile_options = !{!18, !19, !20}
!llvm.ident = !{!21}
!air.version = !{!22}
!air.language_version = !{!23}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 2]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{i32 7, !"air.max_device_buffers", i32 31}
!4 = !{i32 7, !"air.max_constant_buffers", i32 31}
!5 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!6 = !{i32 7, !"air.max_textures", i32 128}
!7 = !{i32 7, !"air.max_read_write_textures", i32 8}
!8 = !{i32 7, !"air.max_samplers", i32 16}
!9 = !{ptr @vector_add, !10, !11}
!10 = !{}
!11 = !{!12, !13, !14, !15, !17}
!12 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"a"}
!13 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"b"}
!14 = !{i32 2, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"result"}
!15 = !{i32 3, !"air.buffer", !"air.location_index", i32 3, i32 1, !"air.read", !"air.address_space", i32 1, !"air.struct_type_info", !16, !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"Params", !"air.arg_name", !"params"}
!16 = !{i32 0, i32 4, i32 0, !"uint", !"count", i32 4, i32 4, i32 0, !"uint", !"offset"}
!17 = !{i32 4, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"i"}
!18 = !{!"air.compile.denorms_disable"}
!19 = !{!"air.compile.fast_math_enable"}
!20 = !{!"air.compile.framebuffer_fetch_enable"}
!21 = !{!"Apple metal version 32023.864 (metalfe-32023.864)"}
!22 = !{i32 2, i32 8, i32 0}
!23 = !{!"Metal", i32 4, i32 0, i32 0}
!24 = !{!25, !26, i64 0}
!25 = !{!"_ZTS6Params", !26, i64 0, !26, i64 4}
!26 = !{!"int", !27, i64 0}
!27 = !{!"omnipotent char", !28, i64 0}
!28 = !{!"Simple C++ TBAA"}
!29 = !{!30}
!30 = distinct !{!30, !31, !"air-alias-scope-arg(3)"}
!31 = distinct !{!31, !"air-alias-scopes(vector_add)"}
!32 = !{!33, !34, !35}
!33 = distinct !{!33, !31, !"air-alias-scope-arg(0)"}
!34 = distinct !{!34, !31, !"air-alias-scope-arg(1)"}
!35 = distinct !{!35, !31, !"air-alias-scope-arg(2)"}
!36 = !{!25, !26, i64 4}
!37 = !{!26, !26, i64 0}
!38 = !{!33}
!39 = !{!34, !35, !30}
!40 = !{!34}
!41 = !{!33, !35, !30}
!42 = !{!35}
!43 = !{!33, !34, !30}
